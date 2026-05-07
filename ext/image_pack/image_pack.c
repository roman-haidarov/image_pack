#include <ruby.h>
#include <ruby/thread.h>
#include <ruby/version.h>
#include <ruby/encoding.h>

#if RUBY_API_VERSION_MAJOR < 3 || (RUBY_API_VERSION_MAJOR == 3 && RUBY_API_VERSION_MINOR < 4)
#error "image_pack requires Ruby 3.4+ (RB_NOGVL_OFFLOAD_SAFE not available)"
#endif

#include <errno.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jpeglib.h>

#ifndef IMAGE_PACK_INIT_EXPORT
#if defined(_WIN32)
#define IMAGE_PACK_INIT_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define IMAGE_PACK_INIT_EXPORT __attribute__((visibility("default")))
#else
#define IMAGE_PACK_INIT_EXPORT
#endif
#endif

#ifndef RB_NOGVL_OFFLOAD_SAFE
#error "RB_NOGVL_OFFLOAD_SAFE is required by image_pack"
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

typedef enum { IP_ALGO_JPEG_TURBO = 1, IP_ALGO_MOZJPEG = 2 } ip_algo_t;

typedef enum {
    IP_EXEC_DIRECT = 1,
    IP_EXEC_NOGVL = 2,
    IP_EXEC_OFFLOAD = 3,
    IP_EXEC_AUTO = 4
} ip_execution_t;

typedef enum { IP_INPUT_BYTES = 1, IP_INPUT_PATH = 2, IP_INPUT_IO_BUFFER = 3 } ip_input_kind_t;

typedef enum { IP_OUTPUT_RETURN_STRING = 1, IP_OUTPUT_PATH = 2 } ip_output_kind_t;

typedef enum {
    IP_OK = 0,
    IP_ERR_INVALID_ARGUMENT,
    IP_ERR_INVALID_IMAGE,
    IP_ERR_UNSUPPORTED,
    IP_ERR_LIMIT,
    IP_ERR_ENCODE,
    IP_ERR_QUALITY,
    IP_ERR_OOM,
    IP_ERR_CANCELLED
} ip_status_t;

typedef enum { IP_OUTPUT_OWNER_NONE = 0, IP_OUTPUT_OWNER_MALLOC = 1 } ip_output_owner_t;

typedef struct {
    const unsigned char *input_data;
    size_t input_size;
    unsigned char *owned_input_data;

    const unsigned char *pixel_data;
    size_t pixel_size;
    unsigned char *owned_pixel_data;
    int width;
    int height;
    int channels;
    int bit_depth;
    size_t decoded_bytes;

    unsigned char *output_data;
    size_t output_size;
    size_t output_capacity;
    ip_output_owner_t output_owner;
    char *output_path;

    int quality;
    int ssim_guard_enabled;
    double min_ssim;
    double measured_ssim;
    int selected_quality;
    int progressive;
    int strip_metadata;
    int mozjpeg_trellis_enabled;
    ip_algo_t algo;
    ip_execution_t requested_execution;
    ip_execution_t resolved_execution;
    int has_scheduler;

    size_t direct_input_threshold;
    size_t direct_pixel_threshold;
    uint64_t max_pixels;
    int max_width;
    int max_height;
    size_t max_output_size;

    ip_status_t status;
    char error_message[512];

    atomic_int cancelled;
    int cancellable_requested;

    jmp_buf jmpbuf;
    int jmp_armed;

    unsigned char *scratch_row;
    size_t scratch_row_size;
} ip_context_t;

typedef struct {
    struct jpeg_error_mgr pub;
    ip_context_t *ctx;
} ip_jpeg_error_mgr;

static VALUE rb_mImagePack;
static VALUE rb_eImagePackError;
static VALUE rb_eImagePackInvalidArgumentError;
static VALUE rb_eImagePackInvalidImageError;
static VALUE rb_eImagePackUnsupportedError;
static VALUE rb_eImagePackLimitExceededError;
static VALUE rb_eImagePackEncodeError;
static VALUE rb_eImagePackQualityConstraintError;
static VALUE rb_eImagePackOutOfMemoryError;
static VALUE rb_eImagePackCancelledError;

static ID id_jpeg_turbo;
static ID id_mozjpeg;
static ID id_direct;
static ID id_nogvl;
static ID id_offload;
static ID id_auto;
static ID id_bytes;
static ID id_path;
static ID id_io_buffer;
static ID id_return_string;
static ID id_configuration;
static ID id_direct_input_threshold;
static ID id_direct_pixel_threshold;
static ID id_max_pixels;
static ID id_max_width;
static ID id_max_height;
static ID id_max_output_size;

static ip_context_t *ip_context_new(void);
static void ip_context_free(ip_context_t *ctx);
static void ip_context_set_error(ip_context_t *ctx, ip_status_t status, const char *message);

static VALUE ip_status_to_exception(ip_status_t status);
static void ip_raise_for_status(ip_context_t *ctx);
static int ip_checked_mul_size(size_t a, size_t b, size_t *out);
static int ip_checked_image_size(int width, int height, int channels, size_t *out);
static void ip_validate_quality_or_raise(ip_context_t *ctx);
static void ip_validate_min_ssim_or_raise(ip_context_t *ctx);
static int ip_bool_value(VALUE value);

static ip_algo_t ip_parse_algo(VALUE sym);
static ip_execution_t ip_parse_execution(VALUE sym);
static ip_input_kind_t ip_parse_input_kind(VALUE sym);
static ip_output_kind_t ip_parse_output_kind(VALUE sym);

static int ip_prepare_input_bytes(ip_context_t *ctx, VALUE input, ip_input_kind_t kind);
static int ip_prepare_pixels(ip_context_t *ctx, VALUE buffer, int width, int height, int channels);
static int ip_prepare_output_path(ip_context_t *ctx, VALUE output, ip_output_kind_t kind);
static VALUE ip_finish_output(ip_context_t *ctx, ip_output_kind_t kind);

static int ip_inspect_jpeg_header(ip_context_t *ctx);
static VALUE ip_inspect_image_entry(VALUE self, VALUE input, VALUE input_kind);

static void ip_resolve_execution(ip_context_t *ctx);
static void ip_unblock_function(void *data);
static void *ip_run_encode_nogvl(void *data);
static int ip_run_context(ip_context_t *ctx);

static void validate_limits_for_pixels(ip_context_t *ctx);

static int ip_jpeg_decode_to_pixels(ip_context_t *ctx, unsigned char **pixels, int *width,
                                    int *height, int *channels, int fast_decode_mode);
static int guarded_compress_jpeg_input_with_mode(ip_context_t *ctx, int mozjpeg_size_mode);
static int ip_jpeg_turbo_compress(ip_context_t *ctx);
static int ip_mozjpeg_compress(ip_context_t *ctx);

#if defined(IMAGE_PACK_ENABLE_LOSSLESS_TRANSCODE_FAST_PATH)
static int ip_jpeg_transcode_coefficients(ip_context_t *ctx, int mozjpeg_size_mode);
#endif

static VALUE ip_compress_jpeg_entry(VALUE self, VALUE input, VALUE input_kind, VALUE output,
                                    VALUE output_kind, VALUE algo, VALUE quality, VALUE min_ssim,
                                    VALUE mozjpeg_trellis, VALUE progressive, VALUE strip_metadata,
                                    VALUE execution, VALUE cancellable, VALUE has_scheduler);
static VALUE ip_compress_pixels_entry(VALUE self, VALUE buffer, VALUE width, VALUE height,
                                      VALUE channels, VALUE output, VALUE output_kind, VALUE algo,
                                      VALUE quality, VALUE progressive, VALUE execution,
                                      VALUE cancellable, VALUE has_scheduler);

static VALUE ip_status_to_exception(ip_status_t status) {
    switch (status) {
    case IP_ERR_INVALID_ARGUMENT:
        return rb_eImagePackInvalidArgumentError;
    case IP_ERR_INVALID_IMAGE:
        return rb_eImagePackInvalidImageError;
    case IP_ERR_UNSUPPORTED:
        return rb_eImagePackUnsupportedError;
    case IP_ERR_LIMIT:
        return rb_eImagePackLimitExceededError;
    case IP_ERR_QUALITY:
        return rb_eImagePackQualityConstraintError;
    case IP_ERR_OOM:
        return rb_eImagePackOutOfMemoryError;
    case IP_ERR_CANCELLED:
        return rb_eImagePackCancelledError;
    case IP_ERR_ENCODE:
    default:
        return rb_eImagePackEncodeError;
    }
}

static void ip_raise_for_status(ip_context_t *ctx) {
    if (!ctx || ctx->status == IP_OK)
        return;

    const char *message = ctx->error_message[0] ? ctx->error_message : "image_pack native error";
    rb_raise(ip_status_to_exception(ctx->status), "%s", message);
}

static int ip_checked_mul_size(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > SIZE_MAX / a)
        return 0;
    *out = a * b;
    return 1;
}

static int ip_checked_image_size(int width, int height, int channels, size_t *out) {
    size_t pixels = 0;

    if (width <= 0 || height <= 0 || channels <= 0)
        return 0;
    if (!ip_checked_mul_size((size_t)width, (size_t)height, &pixels))
        return 0;
    return ip_checked_mul_size(pixels, (size_t)channels, out);
}

static void ip_validate_quality_or_raise(ip_context_t *ctx) {
    if (ctx->quality >= 1 && ctx->quality <= 100)
        return;

    int quality = ctx->quality;
    ip_context_free(ctx);
    rb_raise(rb_eImagePackInvalidArgumentError, "quality must be Integer 1..100, got: %d", quality);
}

static void ip_validate_min_ssim_or_raise(ip_context_t *ctx) {
    if (!ctx->ssim_guard_enabled)
        return;
    if (ctx->min_ssim > 0.0 && ctx->min_ssim <= 1.0)
        return;

    double min_ssim = ctx->min_ssim;
    ip_context_free(ctx);
    rb_raise(rb_eImagePackInvalidArgumentError,
             "min_ssim must be Numeric > 0.0 and <= 1.0, got: %.17g", min_ssim);
}

static int ip_bool_value(VALUE value) {
    if (NIL_P(value) || value == Qfalse)
        return 0;
    if (RB_INTEGER_TYPE_P(value))
        return NUM2INT(value) != 0;
    return 1;
}

static ip_context_t *ip_context_new(void) {
    ip_context_t *ctx = (ip_context_t *)calloc(1, sizeof(ip_context_t));
    if (!ctx)
        return NULL;

    ctx->status = IP_OK;
    ctx->quality = 82;
    ctx->mozjpeg_trellis_enabled = 1;
    ctx->selected_quality = 82;
    ctx->requested_execution = IP_EXEC_AUTO;
    ctx->resolved_execution = IP_EXEC_AUTO;
    ctx->direct_input_threshold = 128 * 1024;
    ctx->direct_pixel_threshold = 1024 * 1024;
    ctx->max_pixels = 100000000ULL;
    ctx->max_width = 30000;
    ctx->max_height = 30000;
    ctx->max_output_size = 256 * 1024 * 1024;
    atomic_init(&ctx->cancelled, 0);
    return ctx;
}

static void ip_context_free(ip_context_t *ctx) {
    if (!ctx)
        return;

    free(ctx->owned_input_data);
    free(ctx->owned_pixel_data);
    free(ctx->output_path);
    free(ctx->scratch_row);

    if (ctx->output_data && ctx->output_owner == IP_OUTPUT_OWNER_MALLOC) {
        free(ctx->output_data);
    }

    free(ctx);
}

static void ip_context_set_error(ip_context_t *ctx, ip_status_t status, const char *message) {
    if (!ctx)
        return;
    ctx->status = status;
    if (!message)
        message = "unknown image_pack native error";
    snprintf(ctx->error_message, sizeof(ctx->error_message), "%s", message);
}

static ID symbol_id(VALUE sym, const char *kind) {
    if (!RB_TYPE_P(sym, T_SYMBOL)) {
        rb_raise(rb_eImagePackInvalidArgumentError, "%s must be a Symbol", kind);
    }

    return SYM2ID(sym);
}

static ip_algo_t ip_parse_algo(VALUE sym) {
    ID id = symbol_id(sym, "algo");
    if (id == id_jpeg_turbo)
        return IP_ALGO_JPEG_TURBO;
    if (id == id_mozjpeg)
        return IP_ALGO_MOZJPEG;
    rb_raise(rb_eImagePackInvalidArgumentError, "unknown algo");
}

static ip_execution_t ip_parse_execution(VALUE sym) {
    ID id = symbol_id(sym, "execution");
    if (id == id_direct)
        return IP_EXEC_DIRECT;
    if (id == id_nogvl)
        return IP_EXEC_NOGVL;
    if (id == id_offload)
        return IP_EXEC_OFFLOAD;
    if (id == id_auto)
        return IP_EXEC_AUTO;
    rb_raise(rb_eImagePackInvalidArgumentError, "unknown execution");
}

static ip_input_kind_t ip_parse_input_kind(VALUE sym) {
    ID id = symbol_id(sym, "input kind");
    if (id == id_bytes)
        return IP_INPUT_BYTES;
    if (id == id_path)
        return IP_INPUT_PATH;
    if (id == id_io_buffer)
        return IP_INPUT_IO_BUFFER;
    rb_raise(rb_eImagePackInvalidArgumentError, "unknown input kind");
}

static ip_output_kind_t ip_parse_output_kind(VALUE sym) {
    ID id = symbol_id(sym, "output kind");
    if (id == id_return_string)
        return IP_OUTPUT_RETURN_STRING;
    if (id == id_path)
        return IP_OUTPUT_PATH;
    rb_raise(rb_eImagePackInvalidArgumentError, "unknown output kind");
}

static VALUE pathname_to_s(VALUE object) {
    if (RB_TYPE_P(object, T_STRING))
        return object;
    return rb_funcall(object, rb_intern("to_s"), 0);
}

static int read_file_to_owned_buffer(ip_context_t *ctx, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        char message[512];
        snprintf(message, sizeof(message), "failed to open input path: %s", path);
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, message);
        return 0;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "failed to seek input path");
        return 0;
    }

    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "failed to determine input size");
        return 0;
    }
    rewind(fp);

    unsigned char *data = (unsigned char *)malloc((size_t)size);
    if (!data && size > 0) {
        fclose(fp);
        ip_context_set_error(ctx, IP_ERR_OOM, "failed to allocate input file buffer");
        return 0;
    }

    size_t read_size = fread(data, 1, (size_t)size, fp);
    fclose(fp);
    if (read_size != (size_t)size) {
        free(data);
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "failed to read input path");
        return 0;
    }

    ctx->owned_input_data = data;
    ctx->input_data = data;
    ctx->input_size = read_size;
    return 1;
}

static VALUE io_buffer_to_string(VALUE buffer) {
    VALUE size = rb_funcall(buffer, rb_intern("size"), 0);
    return rb_funcall(buffer, rb_intern("get_string"), 2, LONG2NUM(0), size);
}

static int ip_prepare_input_bytes(ip_context_t *ctx, VALUE input, ip_input_kind_t kind) {
    if (kind == IP_INPUT_BYTES) {
        Check_Type(input, T_STRING);
        size_t len = (size_t)RSTRING_LEN(input);
        unsigned char *copy = (unsigned char *)malloc(len);
        if (!copy && len > 0) {
            ip_context_set_error(ctx, IP_ERR_OOM, "failed to copy binary String input");
            return 0;
        }
        if (len > 0)
            memcpy(copy, RSTRING_PTR(input), len);
        ctx->owned_input_data = copy;
        ctx->input_data = copy;
        ctx->input_size = len;
        return 1;
    }

    if (kind == IP_INPUT_IO_BUFFER) {
        VALUE str = io_buffer_to_string(input);
        StringValue(str);
        size_t len = (size_t)RSTRING_LEN(str);
        unsigned char *copy = (unsigned char *)malloc(len);
        if (!copy && len > 0) {
            ip_context_set_error(ctx, IP_ERR_OOM, "failed to copy IO::Buffer input");
            return 0;
        }
        if (len > 0)
            memcpy(copy, RSTRING_PTR(str), len);
        ctx->owned_input_data = copy;
        ctx->input_data = copy;
        ctx->input_size = len;
        return 1;
    }

    VALUE path_value = pathname_to_s(input);
    StringValue(path_value);
    return read_file_to_owned_buffer(ctx, StringValueCStr(path_value));
}

static int ip_prepare_pixels(ip_context_t *ctx, VALUE buffer, int width, int height, int channels) {
    VALUE str;
    if (RB_TYPE_P(buffer, T_STRING)) {
        str = buffer;
    } else {
        str = io_buffer_to_string(buffer);
    }

    StringValue(str);
    size_t expected = 0;
    if (!ip_checked_image_size(width, height, channels, &expected)) {
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT,
                             "width * height * channels overflows native size");
        return 0;
    }

    if ((size_t)RSTRING_LEN(str) < expected) {
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT,
                             "pixel buffer is smaller than width * height * channels");
        return 0;
    }

    unsigned char *copy = (unsigned char *)malloc(expected);
    if (!copy && expected > 0) {
        ip_context_set_error(ctx, IP_ERR_OOM, "failed to copy pixel buffer");
        return 0;
    }

    if (expected > 0)
        memcpy(copy, RSTRING_PTR(str), expected);
    ctx->owned_pixel_data = copy;
    ctx->pixel_data = copy;
    ctx->pixel_size = expected;
    ctx->width = width;
    ctx->height = height;
    ctx->channels = channels;
    ctx->bit_depth = 8;
    ctx->decoded_bytes = expected;
    return 1;
}

static int ip_prepare_output_path(ip_context_t *ctx, VALUE output, ip_output_kind_t kind) {
    if (kind == IP_OUTPUT_RETURN_STRING)
        return 1;

    VALUE path_value = pathname_to_s(output);
    StringValue(path_value);
    const char *path = StringValueCStr(path_value);
    ctx->output_path = strdup(path);
    if (!ctx->output_path) {
        ip_context_set_error(ctx, IP_ERR_OOM, "failed to copy output path");
        return 0;
    }
    return 1;
}

static VALUE ip_finish_output(ip_context_t *ctx, ip_output_kind_t kind) {
    ip_raise_for_status(ctx);

    if (kind == IP_OUTPUT_RETURN_STRING) {
        VALUE out = rb_str_new((const char *)ctx->output_data, (long)ctx->output_size);
        rb_enc_associate(out, rb_ascii8bit_encoding());
        return out;
    }

    FILE *fp = fopen(ctx->output_path, "wb");
    if (!fp) {
        rb_raise(rb_eImagePackInvalidArgumentError, "failed to open output path: %s",
                 ctx->output_path);
    }

    size_t written = fwrite(ctx->output_data, 1, ctx->output_size, fp);
    fclose(fp);
    if (written != ctx->output_size) {
        rb_raise(rb_eImagePackEncodeError, "failed to write full JPEG output");
    }

    return Qtrue;
}

static void ip_jpeg_invalid_error_exit(j_common_ptr cinfo) {
    ip_jpeg_error_mgr *err = (ip_jpeg_error_mgr *)cinfo->err;
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    ip_context_set_error(err->ctx, IP_ERR_INVALID_IMAGE, buffer);
    if (err->ctx->jmp_armed)
        longjmp(err->ctx->jmpbuf, 1);
}

static void ip_jpeg_encode_error_exit(j_common_ptr cinfo) {
    ip_jpeg_error_mgr *err = (ip_jpeg_error_mgr *)cinfo->err;
    char buffer[JMSG_LENGTH_MAX];
    (*cinfo->err->format_message)(cinfo, buffer);
    ip_context_set_error(err->ctx, IP_ERR_ENCODE, buffer);
    if (err->ctx->jmp_armed)
        longjmp(err->ctx->jmpbuf, 1);
}

static int ip_inspect_jpeg_header(ip_context_t *ctx) {
    if (!ctx->input_data || ctx->input_size < 2 || ctx->input_data[0] != 0xFF ||
        ctx->input_data[1] != 0xD8) {
        ip_context_set_error(ctx, IP_ERR_INVALID_IMAGE, "input is not a JPEG image");
        return 0;
    }

    struct jpeg_decompress_struct cinfo;
    ip_jpeg_error_mgr jerr;
    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = ip_jpeg_invalid_error_exit;
    jerr.ctx = ctx;

    ctx->jmp_armed = 1;
    if (setjmp(ctx->jmpbuf)) {
        ctx->jmp_armed = 0;
        jpeg_destroy_decompress(&cinfo);
        return 0;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, ctx->input_data, (unsigned long)ctx->input_size);
    int rc = jpeg_read_header(&cinfo, TRUE);
    if (rc != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        ctx->jmp_armed = 0;
        ip_context_set_error(ctx, IP_ERR_INVALID_IMAGE, "invalid JPEG header");
        return 0;
    }

    ctx->width = (int)cinfo.image_width;
    ctx->height = (int)cinfo.image_height;
    ctx->channels = cinfo.num_components;
    if (ctx->channels != 1 && ctx->channels != 3 && ctx->channels != 4) {
        ctx->channels = 3;
    }
    ctx->bit_depth = 8;
    ctx->decoded_bytes = (size_t)ctx->width * (size_t)ctx->height * (size_t)ctx->channels;

    jpeg_destroy_decompress(&cinfo);
    ctx->jmp_armed = 0;

    if (ctx->width <= 0 || ctx->height <= 0) {
        ip_context_set_error(ctx, IP_ERR_INVALID_IMAGE, "invalid JPEG dimensions");
        return 0;
    }

    if (ctx->width > ctx->max_width) {
        ip_context_set_error(ctx, IP_ERR_LIMIT, "image width exceeds max_width");
        return 0;
    }
    if (ctx->height > ctx->max_height) {
        ip_context_set_error(ctx, IP_ERR_LIMIT, "image height exceeds max_height");
        return 0;
    }
    if ((uint64_t)ctx->width * (uint64_t)ctx->height > ctx->max_pixels) {
        ip_context_set_error(ctx, IP_ERR_LIMIT, "image pixels exceed max_pixels");
        return 0;
    }

    return 1;
}

static VALUE ip_inspect_image_entry(VALUE self, VALUE input, VALUE input_kind) {
    (void)self;
    ip_context_t *ctx = ip_context_new();
    if (!ctx)
        rb_raise(rb_eImagePackOutOfMemoryError, "failed to allocate native context");

    if (!ip_prepare_input_bytes(ctx, input, ip_parse_input_kind(input_kind)) ||
        !ip_inspect_jpeg_header(ctx)) {
        VALUE exception = ip_status_to_exception(ctx->status);
        char message[512];
        snprintf(message, sizeof(message), "%s", ctx->error_message);
        ip_context_free(ctx);
        rb_raise(exception, "%s", message[0] ? message : "failed to inspect JPEG image");
    }

    VALUE hash = rb_hash_new();
    rb_hash_aset(hash, ID2SYM(rb_intern("format")), ID2SYM(rb_intern("jpeg")));
    rb_hash_aset(hash, ID2SYM(rb_intern("width")), INT2NUM(ctx->width));
    rb_hash_aset(hash, ID2SYM(rb_intern("height")), INT2NUM(ctx->height));
    rb_hash_aset(hash, ID2SYM(rb_intern("channels")), INT2NUM(ctx->channels));
    rb_hash_aset(hash, ID2SYM(rb_intern("bit_depth")), INT2NUM(ctx->bit_depth));
    rb_hash_aset(hash, ID2SYM(rb_intern("decoded_bytes")), SIZET2NUM(ctx->decoded_bytes));
    ip_context_free(ctx);
    return hash;
}

static J_COLOR_SPACE color_space_for_channels(int channels) {
    return channels == 1 ? JCS_GRAYSCALE : JCS_RGB;
}

static void configure_mozjpeg_profile_before_defaults(struct jpeg_compress_struct *cinfo,
                                                      int mozjpeg_size_mode) {
    jpeg_c_set_int_param(cinfo, JINT_COMPRESS_PROFILE,
                         mozjpeg_size_mode ? JCP_MAX_COMPRESSION : JCP_FASTEST);
}

static void configure_mozjpeg_features_after_defaults(struct jpeg_compress_struct *cinfo,
                                                      int mozjpeg_size_mode,
                                                      int progressive_requested,
                                                      int mozjpeg_trellis_enabled) {
    if (mozjpeg_size_mode) {
        cinfo->optimize_coding = TRUE;

        if (!mozjpeg_trellis_enabled) {
            jpeg_c_set_bool_param(cinfo, JBOOLEAN_TRELLIS_QUANT, FALSE);
            jpeg_c_set_bool_param(cinfo, JBOOLEAN_TRELLIS_QUANT_DC, FALSE);
            jpeg_c_set_bool_param(cinfo, JBOOLEAN_TRELLIS_EOB_OPT, FALSE);
            jpeg_c_set_bool_param(cinfo, JBOOLEAN_USE_SCANS_IN_TRELLIS, FALSE);
            jpeg_c_set_bool_param(cinfo, JBOOLEAN_TRELLIS_Q_OPT, FALSE);
            jpeg_c_set_bool_param(cinfo, JBOOLEAN_OVERSHOOT_DERINGING, FALSE);
        }

        return;
    }

    cinfo->optimize_coding = FALSE;
#if defined(IMAGE_PACK_HAS_SIMD)
    cinfo->dct_method = JDCT_ISLOW;
#else
    cinfo->dct_method = JDCT_FASTEST;
#endif
    cinfo->smoothing_factor = 0;
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_OPTIMIZE_SCANS, FALSE);
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_TRELLIS_QUANT, FALSE);
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_TRELLIS_QUANT_DC, FALSE);
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_TRELLIS_EOB_OPT, FALSE);
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_USE_SCANS_IN_TRELLIS, FALSE);
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_TRELLIS_Q_OPT, FALSE);
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_OVERSHOOT_DERINGING, FALSE);

    if (progressive_requested) {
        jpeg_simple_progression(cinfo);
        cinfo->optimize_coding = TRUE;
    }
}

static int prepare_encode_row(ip_context_t *ctx, JDIMENSION y, JSAMPROW *row) {
    if (ctx->channels == 4) {
        size_t rgb_row_size = 0;
        if (!ip_checked_mul_size((size_t)ctx->width, 3, &rgb_row_size)) {
            ip_context_set_error(ctx, IP_ERR_LIMIT, "RGBA scratch row size overflow");
            return 0;
        }

        if (ctx->scratch_row_size < rgb_row_size) {
            unsigned char *new_row = (unsigned char *)realloc(ctx->scratch_row, rgb_row_size);
            if (!new_row) {
                ip_context_set_error(ctx, IP_ERR_OOM, "failed to allocate RGBA scratch row");
                return 0;
            }
            ctx->scratch_row = new_row;
            ctx->scratch_row_size = rgb_row_size;
        }

        const unsigned char *src = ctx->pixel_data + ((size_t)y * (size_t)ctx->width * 4);
        unsigned char *dst = ctx->scratch_row;
        for (int x = 0; x < ctx->width; x++) {
            dst[x * 3 + 0] = src[x * 4 + 0];
            dst[x * 3 + 1] = src[x * 4 + 1];
            dst[x * 3 + 2] = src[x * 4 + 2];
        }
        *row = ctx->scratch_row;
        return 1;
    }

    *row = (JSAMPROW)(ctx->pixel_data + ((size_t)y * (size_t)ctx->width * (size_t)ctx->channels));
    return 1;
}

static int encode_pixels_with_libjpeg(ip_context_t *ctx, int mozjpeg_size_mode) {
    struct jpeg_compress_struct cinfo;
    ip_jpeg_error_mgr jerr;
    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = ip_jpeg_encode_error_exit;
    jerr.ctx = ctx;

    ctx->jmp_armed = 1;
    if (setjmp(ctx->jmpbuf)) {
        ctx->jmp_armed = 0;
        jpeg_destroy_compress(&cinfo);
        if (ctx->status == IP_OK)
            ip_context_set_error(ctx, IP_ERR_ENCODE, "JPEG encode failed");
        return 0;
    }

    jpeg_create_compress(&cinfo);

    unsigned char *jpeg_buf = NULL;
    unsigned long jpeg_size = 0;
    jpeg_mem_dest(&cinfo, &jpeg_buf, &jpeg_size);

    cinfo.image_width = (JDIMENSION)ctx->width;
    cinfo.image_height = (JDIMENSION)ctx->height;
    cinfo.input_components = ctx->channels == 4 ? 3 : ctx->channels;
    cinfo.in_color_space = color_space_for_channels(cinfo.input_components);

    configure_mozjpeg_profile_before_defaults(&cinfo, mozjpeg_size_mode);
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, ctx->quality, TRUE);
    configure_mozjpeg_features_after_defaults(&cinfo, mozjpeg_size_mode, ctx->progressive,
                                              ctx->mozjpeg_trellis_enabled);

    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        if (ctx->cancellable_requested && (cinfo.next_scanline % 16 == 0) &&
            atomic_load(&ctx->cancelled)) {
            ip_context_set_error(ctx, IP_ERR_CANCELLED, "JPEG encode cancelled");
            jpeg_abort_compress(&cinfo);
            jpeg_destroy_compress(&cinfo);
            free(jpeg_buf);
            ctx->jmp_armed = 0;
            return 0;
        }

        JSAMPROW row = NULL;
        if (!prepare_encode_row(ctx, cinfo.next_scanline, &row)) {
            jpeg_abort_compress(&cinfo);
            jpeg_destroy_compress(&cinfo);
            free(jpeg_buf);
            ctx->jmp_armed = 0;
            return 0;
        }

        jpeg_write_scanlines(&cinfo, &row, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    ctx->jmp_armed = 0;

    if ((size_t)jpeg_size > ctx->max_output_size) {
        free(jpeg_buf);
        ip_context_set_error(ctx, IP_ERR_LIMIT, "output exceeds max_output_size");
        return 0;
    }

    ctx->output_data = jpeg_buf;
    ctx->output_size = (size_t)jpeg_size;
    ctx->output_capacity = (size_t)jpeg_size;
    ctx->output_owner = IP_OUTPUT_OWNER_MALLOC;
    return 1;
}

static int ip_jpeg_decode_to_pixels(ip_context_t *ctx, unsigned char **pixels, int *width,
                                    int *height, int *channels, int fast_decode_mode) {
    struct jpeg_decompress_struct cinfo;
    ip_jpeg_error_mgr jerr;
    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));

    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = ip_jpeg_invalid_error_exit;
    jerr.ctx = ctx;

    ctx->jmp_armed = 1;
    if (setjmp(ctx->jmpbuf)) {
        ctx->jmp_armed = 0;
        jpeg_destroy_decompress(&cinfo);
        if (ctx->status == IP_OK)
            ip_context_set_error(ctx, IP_ERR_INVALID_IMAGE, "JPEG decode failed");
        return 0;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, ctx->input_data, (unsigned long)ctx->input_size);
    int rc = jpeg_read_header(&cinfo, TRUE);
    if (rc != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        ctx->jmp_armed = 0;
        ip_context_set_error(ctx, IP_ERR_INVALID_IMAGE, "invalid JPEG header");
        return 0;
    }

    if (cinfo.image_width > (JDIMENSION)INT_MAX || cinfo.image_height > (JDIMENSION)INT_MAX) {
        jpeg_destroy_decompress(&cinfo);
        ctx->jmp_armed = 0;
        ip_context_set_error(ctx, IP_ERR_LIMIT, "JPEG dimensions exceed native int range");
        return 0;
    }

    int ch = cinfo.num_components == 1 ? 1 : 3;

    ctx->width = (int)cinfo.image_width;
    ctx->height = (int)cinfo.image_height;
    ctx->channels = ch;
    if (!ip_checked_image_size(ctx->width, ctx->height, ctx->channels, &ctx->decoded_bytes)) {
        jpeg_destroy_decompress(&cinfo);
        ctx->jmp_armed = 0;
        ip_context_set_error(ctx, IP_ERR_LIMIT, "decoded image size overflows native size");
        return 0;
    }
    validate_limits_for_pixels(ctx);
    if (ctx->status != IP_OK) {
        jpeg_destroy_decompress(&cinfo);
        ctx->jmp_armed = 0;
        return 0;
    }

    cinfo.out_color_space = ch == 1 ? JCS_GRAYSCALE : JCS_RGB;

    if (fast_decode_mode) {
#if defined(IMAGE_PACK_HAS_SIMD)
        cinfo.dct_method = JDCT_ISLOW;
#else
        cinfo.dct_method = JDCT_FASTEST;
#endif
        cinfo.do_fancy_upsampling = FALSE;
        cinfo.do_block_smoothing = FALSE;
        cinfo.quantize_colors = FALSE;
        cinfo.two_pass_quantize = FALSE;
        cinfo.dither_mode = JDITHER_NONE;
    }

    jpeg_start_decompress(&cinfo);
    size_t row_stride = 0;
    size_t size = 0;
    if (!ip_checked_mul_size((size_t)cinfo.output_width, (size_t)cinfo.output_components,
                             &row_stride) ||
        !ip_checked_mul_size(row_stride, (size_t)cinfo.output_height, &size)) {
        jpeg_destroy_decompress(&cinfo);
        ctx->jmp_armed = 0;
        ip_context_set_error(ctx, IP_ERR_LIMIT, "decoded image buffer size overflow");
        return 0;
    }
    unsigned char *buf = (unsigned char *)malloc(size);
    if (!buf && size > 0) {
        jpeg_destroy_decompress(&cinfo);
        ctx->jmp_armed = 0;
        ip_context_set_error(ctx, IP_ERR_OOM, "failed to allocate decoded pixel buffer");
        return 0;
    }

    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = buf + ((size_t)cinfo.output_scanline * row_stride);
        jpeg_read_scanlines(&cinfo, &row, 1);
    }

    int out_width = (int)cinfo.output_width;
    int out_height = (int)cinfo.output_height;

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    ctx->jmp_armed = 0;

    *pixels = buf;
    *width = out_width;
    *height = out_height;
    *channels = ch;
    return 1;
}

static int compress_jpeg_input_with_mode(ip_context_t *ctx, int mozjpeg_size_mode) {
    if (ctx->pixel_data) {
        return encode_pixels_with_libjpeg(ctx, mozjpeg_size_mode);
    }

    unsigned char *pixels = NULL;
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!ip_jpeg_decode_to_pixels(ctx, &pixels, &width, &height, &channels, !mozjpeg_size_mode))
        return 0;

    ctx->owned_pixel_data = pixels;
    ctx->pixel_data = pixels;
    ctx->pixel_size = (size_t)width * (size_t)height * (size_t)channels;
    ctx->width = width;
    ctx->height = height;
    ctx->channels = channels;
    ctx->decoded_bytes = ctx->pixel_size;

    return encode_pixels_with_libjpeg(ctx, mozjpeg_size_mode);
}

static void ip_clear_output_buffer(ip_context_t *ctx) {
    if (!ctx)
        return;

    if (ctx->output_data && ctx->output_owner == IP_OUTPUT_OWNER_MALLOC)
        free(ctx->output_data);

    ctx->output_data = NULL;
    ctx->output_size = 0;
    ctx->output_capacity = 0;
    ctx->output_owner = IP_OUTPUT_OWNER_NONE;
}

static int ip_decode_jpeg_buffer_preserving_context(ip_context_t *ctx, const unsigned char *data,
                                                    size_t size, unsigned char **pixels, int *width,
                                                    int *height, int *channels) {
    const unsigned char *old_input_data = ctx->input_data;
    size_t old_input_size = ctx->input_size;
    int old_width = ctx->width;
    int old_height = ctx->height;
    int old_channels = ctx->channels;
    int old_bit_depth = ctx->bit_depth;
    size_t old_decoded_bytes = ctx->decoded_bytes;

    ctx->input_data = data;
    ctx->input_size = size;
    int ok = ip_jpeg_decode_to_pixels(ctx, pixels, width, height, channels, 0);

    ctx->input_data = old_input_data;
    ctx->input_size = old_input_size;
    ctx->width = old_width;
    ctx->height = old_height;
    ctx->channels = old_channels;
    ctx->bit_depth = old_bit_depth;
    ctx->decoded_bytes = old_decoded_bytes;
    return ok;
}

static unsigned char *ip_build_luma_buffer(ip_context_t *ctx, const unsigned char *pixels,
                                           int width, int height, int channels) {
    size_t count = 0;
    if (!ip_checked_mul_size((size_t)width, (size_t)height, &count)) {
        ip_context_set_error(ctx, IP_ERR_LIMIT, "luma buffer size overflow");
        return NULL;
    }

    unsigned char *luma = (unsigned char *)malloc(count);
    if (!luma) {
        ip_context_set_error(ctx, IP_ERR_OOM, "failed to allocate luma buffer");
        return NULL;
    }

    if (channels == 1) {
        memcpy(luma, pixels, count);
        return luma;
    }

    for (size_t i = 0; i < count; i++) {
        const unsigned char *p = pixels + (i * (size_t)channels);
        unsigned int y =
            77u * (unsigned int)p[0] + 150u * (unsigned int)p[1] + 29u * (unsigned int)p[2] + 128u;
        luma[i] = (unsigned char)(y >> 8);
    }

    return luma;
}

static double ip_compute_ssim_luma_buffer(const unsigned char *a, const unsigned char *b, int width,
                                          int height) {
    const int window = 8;
    const double c1 = 6.5025;  /* (0.01 * 255)^2 */
    const double c2 = 58.5225; /* (0.03 * 255)^2 */
    double total_ssim = 0.0;
    int windows = 0;

    for (int y0 = 0; y0 < height; y0 += window) {
        int y1 = y0 + window;
        if (y1 > height)
            y1 = height;

        for (int x0 = 0; x0 < width; x0 += window) {
            int x1 = x0 + window;
            if (x1 > width)
                x1 = width;

            double sum_a = 0.0;
            double sum_b = 0.0;
            double sum_a2 = 0.0;
            double sum_b2 = 0.0;
            double sum_ab = 0.0;
            int n = 0;

            for (int y = y0; y < y1; y++) {
                size_t row = (size_t)y * (size_t)width;
                for (int x = x0; x < x1; x++) {
                    size_t idx = row + (size_t)x;
                    double la = (double)a[idx];
                    double lb = (double)b[idx];
                    sum_a += la;
                    sum_b += lb;
                    sum_a2 += la * la;
                    sum_b2 += lb * lb;
                    sum_ab += la * lb;
                    n++;
                }
            }

            if (n <= 0)
                continue;

            double inv_n = 1.0 / (double)n;
            double mean_a = sum_a * inv_n;
            double mean_b = sum_b * inv_n;
            double var_a = (sum_a2 * inv_n) - (mean_a * mean_a);
            double var_b = (sum_b2 * inv_n) - (mean_b * mean_b);
            double cov_ab = (sum_ab * inv_n) - (mean_a * mean_b);

            if (var_a < 0.0)
                var_a = 0.0;
            if (var_b < 0.0)
                var_b = 0.0;

            double numerator = (2.0 * mean_a * mean_b + c1) * (2.0 * cov_ab + c2);
            double denominator = (mean_a * mean_a + mean_b * mean_b + c1) * (var_a + var_b + c2);
            double ssim = denominator == 0.0 ? 1.0 : numerator / denominator;

            if (ssim < 0.0)
                ssim = 0.0;
            if (ssim > 1.0)
                ssim = 1.0;

            total_ssim += ssim;
            windows++;
        }
    }

    return windows > 0 ? total_ssim / (double)windows : 0.0;
}

static int guarded_compress_jpeg_input_with_mode(ip_context_t *ctx, int mozjpeg_size_mode) {
    unsigned char *reference_pixels = NULL;
    int reference_width = 0;
    int reference_height = 0;
    int reference_channels = 0;

    if (ctx->pixel_data) {
        reference_pixels = ctx->owned_pixel_data;
        reference_width = ctx->width;
        reference_height = ctx->height;
        reference_channels = ctx->channels;
    } else {
        if (!ip_jpeg_decode_to_pixels(ctx, &reference_pixels, &reference_width, &reference_height,
                                      &reference_channels, 0)) {
            return 0;
        }

        ctx->owned_pixel_data = reference_pixels;
        ctx->pixel_data = reference_pixels;
        ctx->pixel_size =
            (size_t)reference_width * (size_t)reference_height * (size_t)reference_channels;
        ctx->width = reference_width;
        ctx->height = reference_height;
        ctx->channels = reference_channels;
        ctx->decoded_bytes = ctx->pixel_size;
    }

    if (reference_channels == 4) {
        ip_context_set_error(ctx, IP_ERR_UNSUPPORTED,
                             "min_ssim is not supported for RGBA input in v0.2.0");
        return 0;
    }

    unsigned char *reference_luma = ip_build_luma_buffer(ctx, reference_pixels, reference_width,
                                                         reference_height, reference_channels);
    if (!reference_luma)
        return 0;

    int search_low = ctx->quality;
    int search_high = 100;
    int best_quality = -1;
    double best_ssim = 0.0;
    unsigned char *best_jpeg = NULL;
    size_t best_jpeg_size = 0;
    double best_seen_ssim = 0.0;
    int best_seen_quality = 0;

    while (search_low <= search_high) {
        if (ctx->cancellable_requested && atomic_load(&ctx->cancelled)) {
            free(reference_luma);
            free(best_jpeg);
            ip_context_set_error(ctx, IP_ERR_CANCELLED, "SSIM-guarded JPEG encode cancelled");
            return 0;
        }

        int trial_quality = search_low + ((search_high - search_low) / 2);
        ctx->quality = trial_quality;
        ip_clear_output_buffer(ctx);

        if (!encode_pixels_with_libjpeg(ctx, mozjpeg_size_mode)) {
            free(reference_luma);
            free(best_jpeg);
            return 0;
        }

        unsigned char *candidate_jpeg = ctx->output_data;
        size_t candidate_jpeg_size = ctx->output_size;
        ctx->output_data = NULL;
        ctx->output_size = 0;
        ctx->output_capacity = 0;
        ctx->output_owner = IP_OUTPUT_OWNER_NONE;

        unsigned char *candidate_pixels = NULL;
        int candidate_width = 0;
        int candidate_height = 0;
        int candidate_channels = 0;
        int decoded_ok = ip_decode_jpeg_buffer_preserving_context(
            ctx, candidate_jpeg, candidate_jpeg_size, &candidate_pixels, &candidate_width,
            &candidate_height, &candidate_channels);

        if (!decoded_ok) {
            free(reference_luma);
            free(candidate_jpeg);
            free(best_jpeg);
            return 0;
        }

        if (candidate_width != reference_width || candidate_height != reference_height ||
            candidate_channels != reference_channels) {
            free(reference_luma);
            free(candidate_pixels);
            free(candidate_jpeg);
            free(best_jpeg);
            ip_context_set_error(ctx, IP_ERR_ENCODE,
                                 "candidate JPEG dimensions differ from reference image");
            return 0;
        }

        unsigned char *candidate_luma = ip_build_luma_buffer(ctx, candidate_pixels, candidate_width,
                                                             candidate_height, candidate_channels);
        free(candidate_pixels);

        if (!candidate_luma) {
            free(reference_luma);
            free(candidate_jpeg);
            free(best_jpeg);
            return 0;
        }

        double ssim = ip_compute_ssim_luma_buffer(reference_luma, candidate_luma, reference_width,
                                                  reference_height);
        free(candidate_luma);

        if (ssim > best_seen_ssim) {
            best_seen_ssim = ssim;
            best_seen_quality = trial_quality;
        }

        if (ssim >= ctx->min_ssim) {
            free(best_jpeg);
            best_jpeg = candidate_jpeg;
            best_jpeg_size = candidate_jpeg_size;
            best_quality = trial_quality;
            best_ssim = ssim;
            search_high = trial_quality - 1;
        } else {
            free(candidate_jpeg);
            search_low = trial_quality + 1;
        }
    }

    if (best_quality < 0) {
        char message[512];
        snprintf(message, sizeof(message),
                 "cannot satisfy min_ssim=%.6f; best observed SSIM %.6f at quality=%d",
                 ctx->min_ssim, best_seen_ssim, best_seen_quality);
        free(reference_luma);
        ip_context_set_error(ctx, IP_ERR_QUALITY, message);
        return 0;
    }

    ctx->quality = best_quality;
    ctx->selected_quality = best_quality;
    ctx->measured_ssim = best_ssim;
    free(reference_luma);

    ctx->output_data = best_jpeg;
    ctx->output_size = best_jpeg_size;
    ctx->output_capacity = best_jpeg_size;
    ctx->output_owner = IP_OUTPUT_OWNER_MALLOC;
    return 1;
}

#if defined(IMAGE_PACK_ENABLE_LOSSLESS_TRANSCODE_FAST_PATH)
static int ip_jpeg_transcode_coefficients(ip_context_t *ctx, int mozjpeg_size_mode) {
    if (mozjpeg_size_mode)
        return 0;
    if (!ctx->input_data || ctx->input_size < 4)
        return 0;
    if (ctx->input_data[0] != 0xFF || ctx->input_data[1] != 0xD8)
        return 0;

    struct jpeg_decompress_struct srcinfo;
    struct jpeg_compress_struct dstinfo;
    ip_jpeg_error_mgr jsrcerr;
    ip_jpeg_error_mgr jdsterr;
    memset(&srcinfo, 0, sizeof(srcinfo));
    memset(&dstinfo, 0, sizeof(dstinfo));
    memset(&jsrcerr, 0, sizeof(jsrcerr));
    memset(&jdsterr, 0, sizeof(jdsterr));

    srcinfo.err = jpeg_std_error(&jsrcerr.pub);
    jsrcerr.pub.error_exit = ip_jpeg_invalid_error_exit;
    jsrcerr.ctx = ctx;

    dstinfo.err = jpeg_std_error(&jdsterr.pub);
    jdsterr.pub.error_exit = ip_jpeg_encode_error_exit;
    jdsterr.ctx = ctx;

    unsigned char *jpeg_buf = NULL;
    unsigned long jpeg_size = 0;
    int src_created = 0;
    int dst_created = 0;

    ctx->jmp_armed = 1;
    if (setjmp(ctx->jmpbuf)) {
        ctx->jmp_armed = 0;
        if (dst_created)
            jpeg_destroy_compress(&dstinfo);
        if (src_created)
            jpeg_destroy_decompress(&srcinfo);
        free(jpeg_buf);

        if (ctx->status == IP_OK)
            return 0;
        return 0;
    }

    jpeg_create_decompress(&srcinfo);
    src_created = 1;
    jpeg_mem_src(&srcinfo, ctx->input_data, (unsigned long)ctx->input_size);

    if (jpeg_read_header(&srcinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&srcinfo);
        ctx->jmp_armed = 0;
        return 0;
    }

    if ((int)srcinfo.image_width > ctx->max_width || (int)srcinfo.image_height > ctx->max_height ||
        (uint64_t)srcinfo.image_width * (uint64_t)srcinfo.image_height > ctx->max_pixels) {
        jpeg_destroy_decompress(&srcinfo);
        ctx->jmp_armed = 0;
        ip_context_set_error(ctx, IP_ERR_LIMIT, "image exceeds configured limits");
        return 0;
    }

    jvirt_barray_ptr *src_coef_arrays = jpeg_read_coefficients(&srcinfo);
    if (!src_coef_arrays) {
        jpeg_destroy_decompress(&srcinfo);
        ctx->jmp_armed = 0;
        return 0;
    }

    jpeg_create_compress(&dstinfo);
    dst_created = 1;
    jpeg_mem_dest(&dstinfo, &jpeg_buf, &jpeg_size);
    jpeg_copy_critical_parameters(&srcinfo, &dstinfo);

    dstinfo.optimize_coding = FALSE;
    if (ctx->progressive) {
        jpeg_simple_progression(&dstinfo);
        dstinfo.optimize_coding = TRUE;
    }

    jpeg_write_coefficients(&dstinfo, src_coef_arrays);
    jpeg_finish_compress(&dstinfo);
    jpeg_destroy_compress(&dstinfo);
    dst_created = 0;

    jpeg_finish_decompress(&srcinfo);
    jpeg_destroy_decompress(&srcinfo);
    src_created = 0;

    ctx->jmp_armed = 0;

    if ((size_t)jpeg_size > ctx->max_output_size) {
        free(jpeg_buf);
        ip_context_set_error(ctx, IP_ERR_LIMIT, "transcoded output exceeds max_output_size");
        return 0;
    }

    ctx->output_data = jpeg_buf;
    ctx->output_size = (size_t)jpeg_size;
    ctx->output_capacity = (size_t)jpeg_size;
    ctx->output_owner = IP_OUTPUT_OWNER_MALLOC;

    if (ctx->width == 0 || ctx->height == 0) {
        ctx->width = 0;
        ctx->height = 0;
        ctx->channels = 0;
    }
    return 1;
}

#endif

static int ip_jpeg_turbo_compress(ip_context_t *ctx) {
    if (ctx->ssim_guard_enabled)
        return guarded_compress_jpeg_input_with_mode(ctx, 0);
    return compress_jpeg_input_with_mode(ctx, 0);
}

static int ip_mozjpeg_compress(ip_context_t *ctx) {
    if (ctx->ssim_guard_enabled)
        return guarded_compress_jpeg_input_with_mode(ctx, 1);
    return compress_jpeg_input_with_mode(ctx, 1);
}

static void ip_resolve_execution(ip_context_t *ctx) {
    if (ctx->requested_execution != IP_EXEC_AUTO) {
        ctx->resolved_execution = ctx->requested_execution;
        return;
    }

    if (ctx->cancellable_requested) {
        ctx->resolved_execution = ctx->has_scheduler ? IP_EXEC_OFFLOAD : IP_EXEC_NOGVL;
        return;
    }

    if (ctx->ssim_guard_enabled) {
        ctx->resolved_execution = ctx->has_scheduler ? IP_EXEC_OFFLOAD : IP_EXEC_NOGVL;
        return;
    }

    if (ctx->input_size < ctx->direct_input_threshold &&
        ctx->decoded_bytes < ctx->direct_pixel_threshold) {
        ctx->resolved_execution = IP_EXEC_DIRECT;
        return;
    }

    ctx->resolved_execution = ctx->has_scheduler ? IP_EXEC_OFFLOAD : IP_EXEC_NOGVL;
}

static void ip_unblock_function(void *data) {
    ip_context_t *ctx = (ip_context_t *)data;
    atomic_store(&ctx->cancelled, 1);
}

static void *ip_run_encode_nogvl(void *data) {
    ip_context_t *ctx = (ip_context_t *)data;

    if (ctx->status != IP_OK)
        return NULL;

    switch (ctx->algo) {
    case IP_ALGO_JPEG_TURBO:
        ip_jpeg_turbo_compress(ctx);
        break;
    case IP_ALGO_MOZJPEG:
        ip_mozjpeg_compress(ctx);
        break;
    default:
        ip_context_set_error(ctx, IP_ERR_UNSUPPORTED, "unsupported algorithm");
        break;
    }

    return NULL;
}

static int ip_run_context(ip_context_t *ctx) {
    ip_resolve_execution(ctx);

    if (ctx->resolved_execution == IP_EXEC_DIRECT) {
        ip_run_encode_nogvl(ctx);
    } else if (ctx->resolved_execution == IP_EXEC_NOGVL) {
        rb_nogvl(ip_run_encode_nogvl, ctx, ip_unblock_function, ctx, 0);
    } else if (ctx->resolved_execution == IP_EXEC_OFFLOAD) {
        rb_nogvl(ip_run_encode_nogvl, ctx, ip_unblock_function, ctx, RB_NOGVL_OFFLOAD_SAFE);
    } else {
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "invalid resolved execution mode");
    }

    return ctx->status == IP_OK;
}

static size_t config_size_value(VALUE config, ID id, size_t fallback) {
    VALUE value = rb_funcall(config, id, 0);
    if (NIL_P(value))
        return fallback;
    return NUM2SIZET(value);
}

static int config_int_value(VALUE config, ID id, int fallback) {
    VALUE value = rb_funcall(config, id, 0);
    if (NIL_P(value))
        return fallback;
    return NUM2INT(value);
}

static void apply_configuration(VALUE self, ip_context_t *ctx) {
    VALUE config = rb_funcall(self, id_configuration, 0);
    ctx->direct_input_threshold =
        config_size_value(config, id_direct_input_threshold, ctx->direct_input_threshold);
    ctx->direct_pixel_threshold =
        config_size_value(config, id_direct_pixel_threshold, ctx->direct_pixel_threshold);
    ctx->max_pixels = (uint64_t)config_size_value(config, id_max_pixels, (size_t)ctx->max_pixels);
    ctx->max_width = config_int_value(config, id_max_width, ctx->max_width);
    ctx->max_height = config_int_value(config, id_max_height, ctx->max_height);
    ctx->max_output_size = config_size_value(config, id_max_output_size, ctx->max_output_size);
}

static void validate_limits_for_pixels(ip_context_t *ctx) {
    size_t decoded_bytes = 0;

    if (ctx->width <= 0 || ctx->height <= 0 || ctx->channels <= 0) {
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "image dimensions must be positive");
        return;
    }
    if (ctx->width > ctx->max_width) {
        ip_context_set_error(ctx, IP_ERR_LIMIT, "image width exceeds max_width");
        return;
    }
    if (ctx->height > ctx->max_height) {
        ip_context_set_error(ctx, IP_ERR_LIMIT, "image height exceeds max_height");
        return;
    }
    if ((uint64_t)ctx->width * (uint64_t)ctx->height > ctx->max_pixels) {
        ip_context_set_error(ctx, IP_ERR_LIMIT, "image pixels exceed max_pixels");
        return;
    }
    if (!ip_checked_image_size(ctx->width, ctx->height, ctx->channels, &decoded_bytes)) {
        ip_context_set_error(ctx, IP_ERR_LIMIT, "decoded image size overflows native size");
        return;
    }

    ctx->decoded_bytes = decoded_bytes;
}

static VALUE ip_compress_jpeg_entry(VALUE self, VALUE input, VALUE input_kind, VALUE output,
                                    VALUE output_kind, VALUE algo, VALUE quality, VALUE min_ssim,
                                    VALUE mozjpeg_trellis, VALUE progressive, VALUE strip_metadata,
                                    VALUE execution, VALUE cancellable, VALUE has_scheduler) {
    ip_context_t *ctx = ip_context_new();
    if (!ctx)
        rb_raise(rb_eImagePackOutOfMemoryError, "failed to allocate native context");

    ip_output_kind_t out_kind = ip_parse_output_kind(output_kind);
    ctx->algo = ip_parse_algo(algo);
    ctx->quality = NUM2INT(quality);
    ctx->selected_quality = ctx->quality;
    ip_validate_quality_or_raise(ctx);
    ctx->min_ssim = NUM2DBL(min_ssim);
    ctx->ssim_guard_enabled = ctx->min_ssim > 0.0;
    ip_validate_min_ssim_or_raise(ctx);
    ctx->mozjpeg_trellis_enabled = ip_bool_value(mozjpeg_trellis);
    ctx->progressive = ip_bool_value(progressive);
    ctx->strip_metadata = ip_bool_value(strip_metadata);
    ctx->requested_execution = ip_parse_execution(execution);
    ctx->cancellable_requested = ip_bool_value(cancellable);
    ctx->has_scheduler = ip_bool_value(has_scheduler);
    apply_configuration(self, ctx);

    if (!ip_prepare_input_bytes(ctx, input, ip_parse_input_kind(input_kind)) ||
        !ip_prepare_output_path(ctx, output, out_kind)) {
        VALUE exception = ip_status_to_exception(ctx->status);
        char message[512];
        snprintf(message, sizeof(message), "%s", ctx->error_message);
        ip_context_free(ctx);
        rb_raise(exception, "%s", message[0] ? message : "invalid JPEG input");
    }

    if (ctx->requested_execution == IP_EXEC_AUTO && ctx->input_size < ctx->direct_input_threshold &&
        !ip_inspect_jpeg_header(ctx)) {
        VALUE exception = ip_status_to_exception(ctx->status);
        char message[512];
        snprintf(message, sizeof(message), "%s", ctx->error_message);
        ip_context_free(ctx);
        rb_raise(exception, "%s", message[0] ? message : "invalid JPEG input");
    }

    ip_run_context(ctx);
    VALUE result = ip_finish_output(ctx, out_kind);
    ip_context_free(ctx);
    return result;
}

static VALUE ip_compress_pixels_entry(VALUE self, VALUE buffer, VALUE width, VALUE height,
                                      VALUE channels, VALUE output, VALUE output_kind, VALUE algo,
                                      VALUE quality, VALUE progressive, VALUE execution,
                                      VALUE cancellable, VALUE has_scheduler) {
    ip_context_t *ctx = ip_context_new();
    if (!ctx)
        rb_raise(rb_eImagePackOutOfMemoryError, "failed to allocate native context");

    ip_output_kind_t out_kind = ip_parse_output_kind(output_kind);
    ctx->algo = ip_parse_algo(algo);
    ctx->quality = NUM2INT(quality);
    ip_validate_quality_or_raise(ctx);
    ctx->progressive = ip_bool_value(progressive);
    ctx->strip_metadata = 1;
    ctx->requested_execution = ip_parse_execution(execution);
    ctx->cancellable_requested = ip_bool_value(cancellable);
    ctx->has_scheduler = ip_bool_value(has_scheduler);
    apply_configuration(self, ctx);

    if (!ip_prepare_pixels(ctx, buffer, NUM2INT(width), NUM2INT(height), NUM2INT(channels)) ||
        !ip_prepare_output_path(ctx, output, out_kind)) {
        VALUE exception = ip_status_to_exception(ctx->status);
        char message[512];
        snprintf(message, sizeof(message), "%s", ctx->error_message);
        ip_context_free(ctx);
        rb_raise(exception, "%s", message[0] ? message : "invalid pixel input");
    }

    validate_limits_for_pixels(ctx);
    if (ctx->status != IP_OK) {
        VALUE exception = ip_status_to_exception(ctx->status);
        char message[512];
        snprintf(message, sizeof(message), "%s", ctx->error_message);
        ip_context_free(ctx);
        rb_raise(exception, "%s", message);
    }

    ip_run_context(ctx);
    VALUE result = ip_finish_output(ctx, out_kind);
    ip_context_free(ctx);
    return result;
}

IMAGE_PACK_INIT_EXPORT void Init_image_pack(void) {
    id_jpeg_turbo = rb_intern("jpeg_turbo");
    id_mozjpeg = rb_intern("mozjpeg");
    id_direct = rb_intern("direct");
    id_nogvl = rb_intern("nogvl");
    id_offload = rb_intern("offload");
    id_auto = rb_intern("auto");
    id_bytes = rb_intern("bytes");
    id_path = rb_intern("path");
    id_io_buffer = rb_intern("io_buffer");
    id_return_string = rb_intern("return_string");
    id_configuration = rb_intern("configuration");
    id_direct_input_threshold = rb_intern("direct_input_threshold");
    id_direct_pixel_threshold = rb_intern("direct_pixel_threshold");
    id_max_pixels = rb_intern("max_pixels");
    id_max_width = rb_intern("max_width");
    id_max_height = rb_intern("max_height");
    id_max_output_size = rb_intern("max_output_size");

    rb_mImagePack = rb_define_module("ImagePack");
    rb_eImagePackError = rb_const_get(rb_mImagePack, rb_intern("Error"));
    rb_eImagePackInvalidArgumentError =
        rb_const_get(rb_mImagePack, rb_intern("InvalidArgumentError"));
    rb_eImagePackInvalidImageError = rb_const_get(rb_mImagePack, rb_intern("InvalidImageError"));
    rb_eImagePackUnsupportedError = rb_const_get(rb_mImagePack, rb_intern("UnsupportedError"));
    rb_eImagePackLimitExceededError = rb_const_get(rb_mImagePack, rb_intern("LimitExceededError"));
    rb_eImagePackEncodeError = rb_const_get(rb_mImagePack, rb_intern("EncodeError"));
    rb_eImagePackQualityConstraintError =
        rb_const_get(rb_mImagePack, rb_intern("QualityConstraintError"));
    rb_eImagePackOutOfMemoryError = rb_const_get(rb_mImagePack, rb_intern("OutOfMemoryError"));
    rb_eImagePackCancelledError = rb_const_get(rb_mImagePack, rb_intern("CancelledError"));

    rb_define_singleton_method(rb_mImagePack, "__compress_jpeg", ip_compress_jpeg_entry, 13);
    rb_define_singleton_method(rb_mImagePack, "__compress_pixels", ip_compress_pixels_entry, 12);
    rb_define_singleton_method(rb_mImagePack, "__inspect_image", ip_inspect_image_entry, 2);
}
