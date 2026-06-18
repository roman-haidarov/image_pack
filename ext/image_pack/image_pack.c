#include <ruby.h>
#include <ruby/thread.h>
#include <ruby/version.h>
#include <ruby/encoding.h>

#if defined(RB_NOGVL_OFFLOAD_SAFE)
#define IMAGE_PACK_HAS_OFFLOAD_SAFE 1
#else
#define IMAGE_PACK_HAS_OFFLOAD_SAFE 0
#define RB_NOGVL_OFFLOAD_SAFE       0
#endif

#include <errno.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdint.h>
#include <inttypes.h>
#include <limits.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <jpeglib.h>
#include <jconfigint.h>

#ifndef IMAGE_PACK_INIT_EXPORT
#if defined(_WIN32)
#define IMAGE_PACK_INIT_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define IMAGE_PACK_INIT_EXPORT __attribute__((visibility("default")))
#else
#define IMAGE_PACK_INIT_EXPORT
#endif
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef IMAGE_PACK_MOZJPEG_VERSION
#define IMAGE_PACK_MOZJPEG_VERSION VERSION
#endif

#ifndef FALSE
#define FALSE 0
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define IP_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define IP_RESTRICT __restrict
#else
#define IP_RESTRICT
#endif

#if defined(IMAGE_PACK_HAS_SIMD)
#define IP_FAST_DCT JDCT_ISLOW
#else
#define IP_FAST_DCT JDCT_FASTEST
#endif

#define IP_FAIL(ctx, st, msg)                     \
    do {                                          \
        ip_context_set_error((ctx), (st), (msg)); \
        return 0;                                 \
    } while (0)

#define IP_FAIL_NULL(ctx, st, msg)                \
    do {                                          \
        ip_context_set_error((ctx), (st), (msg)); \
        return NULL;                              \
    } while (0)

#define IP_FAIL_GOTO(ctx, st, msg)                \
    do {                                          \
        ip_context_set_error((ctx), (st), (msg)); \
        goto fail;                                \
    } while (0)

#define IP_ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

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
    int jpeg_color_space;
    size_t decoded_bytes;

    unsigned char *output_data;
    size_t output_size;
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
    int mozjpeg_scan_opt_enabled;
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
    size_t max_input_size;

    ip_status_t status;
    char error_message[512];

    atomic_int cancelled;
    int cancellable_requested;
    int strict;
    int warning_count;
    char first_warning[200];

    jmp_buf jmpbuf;
    int jmp_armed;

    struct {
        int marker;
        unsigned char *data;
        unsigned int len;
    } *preserved_markers;
    size_t preserved_marker_count;
    size_t preserved_marker_capacity;

    unsigned char *transient_jpeg_buf;
    unsigned char *transient_decode_buf;
    int source_orientation;
    int decoded_as_ycbcr;
} ip_context_t;

typedef struct {
    struct jpeg_error_mgr pub;
    ip_context_t *ctx;
} ip_jpeg_error_mgr;

static VALUE rb_mImagePack;
static int ip_offload_runtime_enabled = 1;
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
static ID id_max_input_size;

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
static int ip_value_negative(VALUE value);
static char *ip_strdup(const char *source);
static int ip_replace_file(const char *tmp_path, const char *output_path);

static ip_algo_t ip_parse_algo(VALUE sym);
static ip_execution_t ip_parse_execution(VALUE sym);
static ip_input_kind_t ip_parse_input_kind(VALUE sym);
static ip_output_kind_t ip_parse_output_kind(VALUE sym);

static int ip_prepare_input_bytes(ip_context_t *ctx, VALUE input, ip_input_kind_t kind);
static int ip_prepare_pixels(ip_context_t *ctx, VALUE buffer, int width, int height, int channels,
                             int exact_size);
static int ip_ensure_owned_input_for_async(ip_context_t *ctx, VALUE input, ip_input_kind_t kind);
static int ip_ensure_owned_pixels_for_async(ip_context_t *ctx, VALUE buffer);
static int ip_prepare_output_path(ip_context_t *ctx, VALUE output, ip_output_kind_t kind);
static VALUE ip_finish_output(ip_context_t *ctx, ip_output_kind_t kind);
static void apply_configuration(VALUE self, ip_context_t *ctx);

static int ip_inspect_jpeg_header(ip_context_t *ctx, int allow_cmyk_ycck);
static VALUE ip_inspect_image_entry(VALUE self, VALUE input, VALUE input_kind);

static void ip_resolve_execution(ip_context_t *ctx);
static void ip_unblock_function(void *data);
static void *ip_run_encode_nogvl(void *data);
static int ip_run_context(ip_context_t *ctx);

static void validate_limits_for_pixels(ip_context_t *ctx);

static int ip_jpeg_decode_to_pixels(ip_context_t *ctx, unsigned char **pixels, int *width,
                                    int *height, int *channels, int fast_decode_mode,
                                    int allow_ycbcr_transcode);
static int ip_decode_jpeg_to_luma_buffer(ip_context_t *ctx, const unsigned char *data, size_t size,
                                         unsigned char **luma, int *width, int *height);
static int guarded_compress_jpeg_input_with_mode(ip_context_t *ctx, int mozjpeg_size_mode);
static int ip_jpeg_turbo_compress(ip_context_t *ctx);
static int ip_mozjpeg_compress(ip_context_t *ctx);
static int ip_lossless_optimize_jpeg(ip_context_t *ctx);
static int ip_run_optimize_context(ip_context_t *ctx);

typedef struct {
    VALUE self, input, input_kind, output, output_kind, algo, quality, min_ssim;
    VALUE mozjpeg_trellis, progressive, strip_metadata, execution, cancellable, has_scheduler;
    VALUE report, strict, mozjpeg_scan_opt;
    ip_context_t *ctx;
} ip_compress_jpeg_call_t;

typedef struct {
    VALUE self, buffer, width, height, channels, output, output_kind, algo, quality, min_ssim;
    VALUE mozjpeg_trellis, progressive, exact_size, execution, cancellable, has_scheduler;
    VALUE report, strict, mozjpeg_scan_opt;
    ip_context_t *ctx;
} ip_compress_pixels_call_t;

typedef struct {
    VALUE self, input, input_kind, output, output_kind, progressive, strip_metadata;
    VALUE execution, cancellable, has_scheduler, strict;
    ip_context_t *ctx;
} ip_optimize_jpeg_call_t;

typedef struct {
    VALUE self, input, input_kind;
    ip_context_t *ctx;
} ip_inspect_call_t;

static VALUE ip_call_cleanup(VALUE ptr) {
    ip_context_t **ctx_ptr = (ip_context_t **)ptr;
    if (ctx_ptr && *ctx_ptr) {
        ip_context_free(*ctx_ptr);
        *ctx_ptr = NULL;
    }
    return Qnil;
}

static VALUE ip_compress_jpeg_entry(int argc, VALUE *argv, VALUE self);
static VALUE ip_compress_pixels_entry(int argc, VALUE *argv, VALUE self);
static VALUE ip_optimize_jpeg_entry(VALUE self, VALUE input, VALUE input_kind, VALUE output,
                                    VALUE output_kind, VALUE progressive, VALUE strip_metadata,
                                    VALUE execution, VALUE cancellable, VALUE has_scheduler,
                                    VALUE strict);

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
    rb_raise(rb_eImagePackInvalidArgumentError, "quality must be Integer 1..100, got: %d", quality);
}

static void ip_validate_min_ssim_or_raise(ip_context_t *ctx) {
    if (!ctx->ssim_guard_enabled)
        return;
    if (ctx->min_ssim > 0.0 && ctx->min_ssim <= 1.0)
        return;

    double min_ssim = ctx->min_ssim;
    rb_raise(rb_eImagePackInvalidArgumentError,
             "min_ssim must be Numeric > 0.0 and <= 1.0, got: %.17g", min_ssim);
}

static int ip_value_negative(VALUE value) {
    return RTEST(rb_funcall(value, rb_intern("<"), 1, INT2FIX(0)));
}

static char *ip_strdup(const char *source) {
    size_t len = strlen(source) + 1;
    char *copy = (char *)malloc(len);
    if (!copy)
        return NULL;
    memcpy(copy, source, len);
    return copy;
}

#if defined(_WIN32)
static int ip_replace_file(const char *tmp_path, const char *output_path) {
    return MoveFileExA(tmp_path, output_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)
               ? 0
               : -1;
}
#else
static int ip_replace_file(const char *tmp_path, const char *output_path) {
    return rename(tmp_path, output_path);
}
#endif

static int ip_file_seek_end(FILE *fp) {
#if defined(_WIN32)
    return _fseeki64(fp, 0, SEEK_END);
#else
    return fseeko(fp, 0, SEEK_END);
#endif
}

static int ip_file_rewind(FILE *fp) {
#if defined(_WIN32)
    return _fseeki64(fp, 0, SEEK_SET);
#else
    return fseeko(fp, 0, SEEK_SET);
#endif
}

static int ip_file_tell(FILE *fp, size_t *out) {
#if defined(_WIN32)
    __int64 pos = _ftelli64(fp);
    if (pos < 0)
        return 0;
    if ((unsigned long long)pos > (unsigned long long)SIZE_MAX)
        return 0;
    *out = (size_t)pos;
    return 1;
#else
    off_t pos = ftello(fp);
    if (pos < 0)
        return 0;
    if ((uintmax_t)pos > (uintmax_t)SIZE_MAX)
        return 0;
    *out = (size_t)pos;
    return 1;
#endif
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
    ctx->mozjpeg_scan_opt_enabled = 1;
    ctx->selected_quality = 82;
    ctx->requested_execution = IP_EXEC_AUTO;
    ctx->resolved_execution = IP_EXEC_AUTO;
    ctx->direct_input_threshold = 128 * 1024;
    ctx->direct_pixel_threshold = 1024 * 1024;
    ctx->max_pixels = 100000000ULL;
    ctx->max_width = 30000;
    ctx->max_height = 30000;
    ctx->max_output_size = 256 * 1024 * 1024;
    ctx->max_input_size = 256 * 1024 * 1024;
    ctx->source_orientation = 1;
    atomic_init(&ctx->cancelled, 0);
    return ctx;
}

static void ip_context_free(ip_context_t *ctx) {
    if (!ctx)
        return;

    free(ctx->owned_input_data);
    free(ctx->owned_pixel_data);
    free(ctx->output_path);
    free(ctx->transient_jpeg_buf);
    free(ctx->transient_decode_buf);

    if (ctx->preserved_markers) {
        for (size_t i = 0; i < ctx->preserved_marker_count; i++) {
            free(ctx->preserved_markers[i].data);
        }
        free(ctx->preserved_markers);
    }

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

typedef struct {
    const ID *id;
    int value;
} ip_symbol_entry;

static int ip_map_symbol(VALUE sym, const char *kind, const ip_symbol_entry *table) {
    ID id = symbol_id(sym, kind);
    for (; table->id != NULL; table++) {
        if (id == *table->id)
            return table->value;
    }
    rb_raise(rb_eImagePackInvalidArgumentError, "unknown %s", kind);
}

static ip_algo_t ip_parse_algo(VALUE sym) {
    static const ip_symbol_entry table[] = {
        {&id_jpeg_turbo, IP_ALGO_JPEG_TURBO}, {&id_mozjpeg, IP_ALGO_MOZJPEG}, {NULL, 0}};
    return (ip_algo_t)ip_map_symbol(sym, "algo", table);
}

static ip_execution_t ip_parse_execution(VALUE sym) {
    static const ip_symbol_entry table[] = {{&id_direct, IP_EXEC_DIRECT},
                                            {&id_nogvl, IP_EXEC_NOGVL},
                                            {&id_offload, IP_EXEC_OFFLOAD},
                                            {&id_auto, IP_EXEC_AUTO},
                                            {NULL, 0}};
    return (ip_execution_t)ip_map_symbol(sym, "execution", table);
}

static ip_input_kind_t ip_parse_input_kind(VALUE sym) {
    static const ip_symbol_entry table[] = {{&id_bytes, IP_INPUT_BYTES},
                                            {&id_path, IP_INPUT_PATH},
                                            {&id_io_buffer, IP_INPUT_IO_BUFFER},
                                            {NULL, 0}};
    return (ip_input_kind_t)ip_map_symbol(sym, "input kind", table);
}

static ip_output_kind_t ip_parse_output_kind(VALUE sym) {
    static const ip_symbol_entry table[] = {
        {&id_return_string, IP_OUTPUT_RETURN_STRING}, {&id_path, IP_OUTPUT_PATH}, {NULL, 0}};
    return (ip_output_kind_t)ip_map_symbol(sym, "output kind", table);
}

static VALUE ip_sym(const char *name) {
    return ID2SYM(rb_intern(name));
}

static void ip_jpeg_emit_message_collect(j_common_ptr cinfo, int msg_level) {
    if (msg_level >= 0)
        return;

    ip_jpeg_error_mgr *err = (ip_jpeg_error_mgr *)cinfo->err;
    ip_context_t *ctx = err->ctx;
    if (!ctx)
        return;

    ctx->warning_count++;
    if (ctx->first_warning[0] == '\0') {
        char buffer[JMSG_LENGTH_MAX];
        (*cinfo->err->format_message)(cinfo, buffer);
        snprintf(ctx->first_warning, sizeof(ctx->first_warning), "%s", buffer);
    }

    if (ctx->strict) {
        if (ctx->status == IP_OK) {
            char buffer[JMSG_LENGTH_MAX];
            (*cinfo->err->format_message)(cinfo, buffer);
            ip_context_set_error(ctx, IP_ERR_INVALID_IMAGE, buffer);
        }
        if (ctx->jmp_armed)
            longjmp(ctx->jmpbuf, 1);
    }
}

static void ip_jpeg_output_message_silent(j_common_ptr cinfo) {
    (void)cinfo;
}

static struct jpeg_error_mgr *ip_use_error(ip_jpeg_error_mgr *jerr, ip_context_t *ctx,
                                           void (*handler)(j_common_ptr)) {
    struct jpeg_error_mgr *base = jpeg_std_error(&jerr->pub);
    jerr->pub.error_exit = handler;
    jerr->pub.emit_message = ip_jpeg_emit_message_collect;
    jerr->pub.output_message = ip_jpeg_output_message_silent;
    jerr->ctx = ctx;
    return base;
}

static void ip_apply_fast_decode(struct jpeg_decompress_struct *cinfo) {
    cinfo->dct_method = IP_FAST_DCT;
    cinfo->do_fancy_upsampling = FALSE;
    cinfo->do_block_smoothing = FALSE;
    cinfo->quantize_colors = FALSE;
    cinfo->two_pass_quantize = FALSE;
    cinfo->dither_mode = JDITHER_NONE;
}

static void ip_disable_mozjpeg_trellis(struct jpeg_compress_struct *cinfo) {
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_TRELLIS_QUANT, FALSE);
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_TRELLIS_QUANT_DC, FALSE);
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_TRELLIS_EOB_OPT, FALSE);
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_USE_SCANS_IN_TRELLIS, FALSE);
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_TRELLIS_Q_OPT, FALSE);
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_OVERSHOOT_DERINGING, FALSE);
}

static int ip_check_max_dimension_limits(ip_context_t *ctx) {
    if (ctx->max_width > 0 && ctx->width > ctx->max_width)
        IP_FAIL(ctx, IP_ERR_LIMIT, "image width exceeds max_width");
    if (ctx->max_height > 0 && ctx->height > ctx->max_height)
        IP_FAIL(ctx, IP_ERR_LIMIT, "image height exceeds max_height");
    if (ctx->max_pixels > 0 && (uint64_t)ctx->width * (uint64_t)ctx->height > ctx->max_pixels)
        IP_FAIL(ctx, IP_ERR_LIMIT, "image pixels exceed max_pixels");
    return 1;
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

    if (ip_file_seek_end(fp) != 0) {
        fclose(fp);
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "failed to seek input path");
        return 0;
    }

    size_t size = 0;
    if (!ip_file_tell(fp, &size)) {
        fclose(fp);
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "failed to determine input size");
        return 0;
    }

    if (ip_file_rewind(fp) != 0) {
        fclose(fp);
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "failed to rewind input path");
        return 0;
    }

    if (ctx->max_input_size > 0 && size > ctx->max_input_size) {
        fclose(fp);
        ip_context_set_error(ctx, IP_ERR_LIMIT, "input file exceeds max_input_size");
        return 0;
    }

    unsigned char *data = (unsigned char *)malloc(size);
    if (!data && size > 0) {
        fclose(fp);
        ip_context_set_error(ctx, IP_ERR_OOM, "failed to allocate input file buffer");
        return 0;
    }

    size_t read_size = fread(data, 1, size, fp);
    fclose(fp);
    if (read_size != size) {
        free(data);
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "failed to read input path");
        return 0;
    }

    ctx->owned_input_data = data;
    ctx->input_data = data;
    ctx->input_size = read_size;
    return 1;
}

static size_t io_buffer_size_or_raise(VALUE buffer) {
    VALUE size_value = rb_funcall(buffer, rb_intern("size"), 0);
    if (!RB_INTEGER_TYPE_P(size_value) || ip_value_negative(size_value))
        rb_raise(rb_eImagePackInvalidArgumentError, "IO::Buffer#size must return Integer >= 0");
    return NUM2SIZET(size_value);
}

static VALUE io_buffer_to_string_slice(VALUE buffer, size_t len) {
    return rb_funcall(buffer, rb_intern("get_string"), 2, LONG2NUM(0), SIZET2NUM(len));
}

static int ip_copy_string_to_owned_input(ip_context_t *ctx, VALUE input) {
    Check_Type(input, T_STRING);
    size_t len = (size_t)RSTRING_LEN(input);
    if (ctx->max_input_size > 0 && len > ctx->max_input_size) {
        ip_context_set_error(ctx, IP_ERR_LIMIT, "input bytes exceed max_input_size");
        return 0;
    }

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

static int ip_prepare_input_bytes(ip_context_t *ctx, VALUE input, ip_input_kind_t kind) {
    if (kind == IP_INPUT_BYTES) {
        Check_Type(input, T_STRING);
        size_t len = (size_t)RSTRING_LEN(input);
        if (ctx->max_input_size > 0 && len > ctx->max_input_size) {
            ip_context_set_error(ctx, IP_ERR_LIMIT, "input bytes exceed max_input_size");
            return 0;
        }

        if (ctx->requested_execution == IP_EXEC_DIRECT ||
            ctx->requested_execution == IP_EXEC_AUTO) {
            ctx->input_data = (const unsigned char *)RSTRING_PTR(input);
            ctx->input_size = len;
            return 1;
        }

        return ip_copy_string_to_owned_input(ctx, input);
    }

    if (kind == IP_INPUT_IO_BUFFER) {
        size_t len = io_buffer_size_or_raise(input);
        if (ctx->max_input_size > 0 && len > ctx->max_input_size) {
            ip_context_set_error(ctx, IP_ERR_LIMIT, "input IO::Buffer exceeds max_input_size");
            return 0;
        }

        VALUE str = io_buffer_to_string_slice(input, len);
        StringValue(str);
        if ((size_t)RSTRING_LEN(str) != len) {
            ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT,
                                 "IO::Buffer#get_string returned unexpected size");
            return 0;
        }

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

static int ip_ensure_owned_input_for_async(ip_context_t *ctx, VALUE input, ip_input_kind_t kind) {
    if (ctx->resolved_execution == IP_EXEC_DIRECT)
        return 1;
    if (kind != IP_INPUT_BYTES || ctx->owned_input_data)
        return 1;
    return ip_copy_string_to_owned_input(ctx, input);
}

static int ip_copy_string_to_owned_pixels(ip_context_t *ctx, VALUE str, size_t expected) {
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
    return 1;
}

static int ip_prepare_pixels(ip_context_t *ctx, VALUE buffer, int width, int height, int channels,
                             int exact_size) {
    size_t expected = 0;
    if (!ip_checked_image_size(width, height, channels, &expected)) {
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT,
                             "width * height * channels overflows native size");
        return 0;
    }

    VALUE str = Qnil;
    size_t actual = 0;
    int buffer_is_string = RB_TYPE_P(buffer, T_STRING);

    if (buffer_is_string) {
        str = buffer;
        actual = (size_t)RSTRING_LEN(str);
    } else {
        actual = io_buffer_size_or_raise(buffer);
    }

    if (actual < expected) {
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT,
                             "pixel buffer is smaller than width * height * channels");
        return 0;
    }

    if (exact_size && actual != expected) {
        ip_context_set_error(
            ctx, IP_ERR_INVALID_ARGUMENT,
            "pixel buffer size must equal width * height * channels when exact_size is true");
        return 0;
    }

    if (!buffer_is_string) {
        str = io_buffer_to_string_slice(buffer, expected);
        StringValue(str);
        if ((size_t)RSTRING_LEN(str) != expected) {
            ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT,
                                 "IO::Buffer#get_string returned unexpected pixel size");
            return 0;
        }
    }

    if ((ctx->requested_execution == IP_EXEC_DIRECT || ctx->requested_execution == IP_EXEC_AUTO) &&
        buffer_is_string) {
        ctx->pixel_data = (const unsigned char *)RSTRING_PTR(str);
        ctx->pixel_size = expected;
    } else {
        if (!ip_copy_string_to_owned_pixels(ctx, str, expected))
            return 0;
    }

    ctx->width = width;
    ctx->height = height;
    ctx->channels = channels;
    ctx->bit_depth = 8;
    ctx->decoded_bytes = expected;
    return 1;
}

static int ip_ensure_owned_pixels_for_async(ip_context_t *ctx, VALUE buffer) {
    if (ctx->resolved_execution == IP_EXEC_DIRECT)
        return 1;
    if (!RB_TYPE_P(buffer, T_STRING) || ctx->owned_pixel_data)
        return 1;
    return ip_copy_string_to_owned_pixels(ctx, buffer, ctx->pixel_size);
}

static int ip_prepare_output_path(ip_context_t *ctx, VALUE output, ip_output_kind_t kind) {
    if (kind == IP_OUTPUT_RETURN_STRING)
        return 1;

    VALUE path_value = pathname_to_s(output);
    StringValue(path_value);
    const char *path = StringValueCStr(path_value);
    ctx->output_path = ip_strdup(path);
    if (!ctx->output_path) {
        ip_context_set_error(ctx, IP_ERR_OOM, "failed to copy output path");
        return 0;
    }
    return 1;
}

static VALUE ip_finish_output(ip_context_t *ctx, ip_output_kind_t kind) {
    ip_raise_for_status(ctx);

    if (kind == IP_OUTPUT_RETURN_STRING) {
        if (ctx->output_size > (size_t)LONG_MAX)
            rb_raise(rb_eImagePackLimitExceededError, "output is too large for a Ruby String");
        VALUE out = rb_str_new((const char *)ctx->output_data, (long)ctx->output_size);
        rb_enc_associate(out, rb_ascii8bit_encoding());
        return out;
    }

    size_t path_len = strlen(ctx->output_path);
    char suffix[64];
    snprintf(suffix, sizeof(suffix), ".tmp.image_pack.%p", (void *)ctx);
    size_t tmp_len = path_len + strlen(suffix) + 1;
    char *tmp_path = (char *)malloc(tmp_len);
    if (!tmp_path)
        rb_raise(rb_eImagePackOutOfMemoryError, "failed to allocate temporary output path");
    snprintf(tmp_path, tmp_len, "%s%s", ctx->output_path, suffix);

    FILE *fp = fopen(tmp_path, "wb");
    if (!fp) {
        free(tmp_path);
        rb_raise(rb_eImagePackInvalidArgumentError, "failed to open output path: %s",
                 ctx->output_path);
    }

    size_t written = fwrite(ctx->output_data, 1, ctx->output_size, fp);
    int write_failed = written != ctx->output_size || ferror(fp);
    int close_failed = fclose(fp) != 0;
    if (write_failed || close_failed) {
        remove(tmp_path);
        free(tmp_path);
        rb_raise(rb_eImagePackEncodeError, "failed to write full JPEG output");
    }

    if (ip_replace_file(tmp_path, ctx->output_path) != 0) {
        remove(tmp_path);
        free(tmp_path);
        rb_raise(rb_eImagePackEncodeError, "failed to move temporary JPEG output into place");
    }

    free(tmp_path);
    return Qtrue;
}

static VALUE ip_build_report(ip_context_t *ctx, VALUE output_value) {
    VALUE hash = rb_hash_new();
    rb_hash_aset(hash, ip_sym("output"), output_value);
    rb_hash_aset(hash, ip_sym("quality"), INT2NUM(ctx->selected_quality));
    rb_hash_aset(hash, ip_sym("ssim"),
                 ctx->ssim_guard_enabled ? DBL2NUM(ctx->measured_ssim) : Qnil);
    rb_hash_aset(hash, ip_sym("algo"),
                 ctx->algo == IP_ALGO_MOZJPEG ? ip_sym("mozjpeg") : ip_sym("jpeg_turbo"));
    rb_hash_aset(hash, ip_sym("bytesize"), SIZET2NUM(ctx->output_size));
    rb_hash_aset(hash, ip_sym("input_bytesize"),
                 SIZET2NUM(ctx->input_size > 0 ? ctx->input_size : ctx->pixel_size));
    rb_hash_aset(hash, ip_sym("warning_count"), INT2NUM(ctx->warning_count));
    rb_hash_aset(hash, ip_sym("warning"),
                 ctx->first_warning[0] ? rb_str_new_cstr(ctx->first_warning) : Qnil);
    return hash;
}

static int ip_save_marker(ip_context_t *ctx, int marker, const unsigned char *data,
                          unsigned int len) {
    if (ctx->preserved_marker_count == ctx->preserved_marker_capacity) {
        if (ctx->preserved_marker_capacity > SIZE_MAX / 2)
            return 0;
        size_t new_cap =
            ctx->preserved_marker_capacity == 0 ? 4 : ctx->preserved_marker_capacity * 2;
        if (new_cap > SIZE_MAX / sizeof(*ctx->preserved_markers))
            return 0;
        void *new_buf = realloc(ctx->preserved_markers, new_cap * sizeof(*ctx->preserved_markers));
        if (!new_buf)
            return 0;
        ctx->preserved_markers = new_buf;
        ctx->preserved_marker_capacity = new_cap;
    }

    unsigned char *copy = (unsigned char *)malloc(len);
    if (!copy && len > 0)
        return 0;
    if (len > 0)
        memcpy(copy, data, len);

    ctx->preserved_markers[ctx->preserved_marker_count].marker = marker;
    ctx->preserved_markers[ctx->preserved_marker_count].data = copy;
    ctx->preserved_markers[ctx->preserved_marker_count].len = len;
    ctx->preserved_marker_count++;
    return 1;
}

static int ip_save_markers_from_decompress(ip_context_t *ctx,
                                           struct jpeg_decompress_struct *cinfo) {
    jpeg_saved_marker_ptr m;
    for (m = cinfo->marker_list; m != NULL; m = m->next) {
        if (m->marker == (JPEG_APP0 + 0))
            continue;

        if (!ip_save_marker(ctx, m->marker, m->data, m->data_length)) {
            ip_context_set_error(ctx, IP_ERR_OOM, "failed to preserve JPEG metadata marker");
            return 0;
        }
    }
    return 1;
}

static uint16_t ip_exif_u16(const unsigned char *p, int little_endian) {
    if (little_endian)
        return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint32_t ip_exif_u32(const unsigned char *p, int little_endian) {
    if (little_endian)
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
               ((uint32_t)p[3] << 24);
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int ip_parse_exif_orientation(const unsigned char *data, unsigned int len) {
    if (!data || len < 14)
        return 1;
    if (memcmp(data, "Exif\0\0", 6) != 0)
        return 1;

    const unsigned char *tiff = data + 6;
    size_t tiff_len = (size_t)len - 6;
    if (tiff_len < 8)
        return 1;

    int little_endian = 0;
    if (tiff[0] == 'I' && tiff[1] == 'I')
        little_endian = 1;
    else if (tiff[0] == 'M' && tiff[1] == 'M')
        little_endian = 0;
    else
        return 1;

    if (ip_exif_u16(tiff + 2, little_endian) != 42)
        return 1;

    uint32_t ifd0_offset = ip_exif_u32(tiff + 4, little_endian);
    if (ifd0_offset > tiff_len || tiff_len - ifd0_offset < 2)
        return 1;

    const unsigned char *ifd = tiff + ifd0_offset;
    size_t ifd_len = tiff_len - ifd0_offset;
    uint16_t entries = ip_exif_u16(ifd, little_endian);
    ifd += 2;
    ifd_len -= 2;

    for (uint16_t i = 0; i < entries; i++) {
        if (ifd_len < 12)
            return 1;

        uint16_t tag = ip_exif_u16(ifd, little_endian);
        uint16_t type = ip_exif_u16(ifd + 2, little_endian);
        uint32_t count = ip_exif_u32(ifd + 4, little_endian);

        if (tag == 0x0112 && type == 3 && count == 1) {
            uint16_t orientation = ip_exif_u16(ifd + 8, little_endian);
            if (orientation >= 1 && orientation <= 8)
                return (int)orientation;
            return 1;
        }

        ifd += 12;
        ifd_len -= 12;
    }

    return 1;
}

static int ip_read_exif_orientation_from_decompress(struct jpeg_decompress_struct *cinfo) {
    jpeg_saved_marker_ptr m;
    for (m = cinfo->marker_list; m != NULL; m = m->next) {
        if (m->marker == (JPEG_APP0 + 1)) {
            int orientation = ip_parse_exif_orientation(m->data, m->data_length);
            if (orientation >= 2 && orientation <= 8)
                return orientation;
        }
    }
    return 1;
}

static int ip_transform_pixels_for_orientation(ip_context_t *ctx, unsigned char **pixels,
                                               int *width, int *height, int channels) {
    int orientation = ctx->source_orientation;
    if (orientation <= 1 || orientation > 8)
        return 1;

    int src_w = *width;
    int src_h = *height;
    int dst_w = (orientation >= 5 && orientation <= 8) ? src_h : src_w;
    int dst_h = (orientation >= 5 && orientation <= 8) ? src_w : src_h;
    size_t out_size = 0;
    if (!ip_checked_image_size(dst_w, dst_h, channels, &out_size)) {
        ip_context_set_error(ctx, IP_ERR_LIMIT, "oriented image size overflows native size");
        return 0;
    }

    unsigned char *src = *pixels;
    unsigned char *dst = (unsigned char *)malloc(out_size);
    if (!dst && out_size > 0) {
        ip_context_set_error(ctx, IP_ERR_OOM, "failed to allocate EXIF-oriented pixel buffer");
        return 0;
    }

    for (int y = 0; y < src_h; y++) {
        for (int x = 0; x < src_w; x++) {
            int dx = x;
            int dy = y;
            switch (orientation) {
            case 2:
                dx = src_w - 1 - x;
                dy = y;
                break;
            case 3:
                dx = src_w - 1 - x;
                dy = src_h - 1 - y;
                break;
            case 4:
                dx = x;
                dy = src_h - 1 - y;
                break;
            case 5:
                dx = y;
                dy = x;
                break;
            case 6:
                dx = src_h - 1 - y;
                dy = x;
                break;
            case 7:
                dx = src_h - 1 - y;
                dy = src_w - 1 - x;
                break;
            case 8:
                dx = y;
                dy = src_w - 1 - x;
                break;
            default:
                break;
            }

            memcpy(dst + (((size_t)dy * (size_t)dst_w + (size_t)dx) * (size_t)channels),
                   src + (((size_t)y * (size_t)src_w + (size_t)x) * (size_t)channels),
                   (size_t)channels);
        }
    }

    free(src);
    *pixels = dst;
    *width = dst_w;
    *height = dst_h;
    return 1;
}

static void ip_write_preserved_markers(ip_context_t *ctx, struct jpeg_compress_struct *cinfo) {
    for (size_t i = 0; i < ctx->preserved_marker_count; i++) {
        jpeg_write_marker(cinfo, ctx->preserved_markers[i].marker,
                          (const JOCTET *)ctx->preserved_markers[i].data,
                          ctx->preserved_markers[i].len);
    }
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

static const char *ip_jpeg_color_space_name(int color_space) {
    switch ((J_COLOR_SPACE)color_space) {
    case JCS_GRAYSCALE:
        return "grayscale";
    case JCS_RGB:
        return "rgb";
    case JCS_YCbCr:
        return "ycbcr";
    case JCS_CMYK:
        return "cmyk";
    case JCS_YCCK:
        return "ycck";
    default:
        return "unknown";
    }
}

static int ip_inspect_jpeg_header(ip_context_t *ctx, int allow_cmyk_ycck) {
    if (!ctx->input_data || ctx->input_size < 2 || ctx->input_data[0] != 0xFF ||
        ctx->input_data[1] != 0xD8)
        IP_FAIL(ctx, IP_ERR_INVALID_IMAGE, "input is not a JPEG image");

    struct jpeg_decompress_struct cinfo;
    ip_jpeg_error_mgr jerr;
    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));
    cinfo.err = ip_use_error(&jerr, ctx, ip_jpeg_invalid_error_exit);

    ctx->jmp_armed = 1;
    if (setjmp(ctx->jmpbuf))
        goto fail;

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, ctx->input_data, (unsigned long)ctx->input_size);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
        IP_FAIL_GOTO(ctx, IP_ERR_INVALID_IMAGE, "invalid JPEG header");

    if (!allow_cmyk_ycck && (cinfo.num_components == 4 || cinfo.jpeg_color_space == JCS_CMYK ||
                             cinfo.jpeg_color_space == JCS_YCCK))
        IP_FAIL_GOTO(ctx, IP_ERR_UNSUPPORTED,
                     "CMYK/YCCK JPEG input is not supported for pixel recompression");

    ctx->width = (int)cinfo.image_width;
    ctx->height = (int)cinfo.image_height;
    ctx->channels = cinfo.num_components;
    ctx->jpeg_color_space = (int)cinfo.jpeg_color_space;

    jpeg_destroy_decompress(&cinfo);
    ctx->jmp_armed = 0;

    if (ctx->width <= 0 || ctx->height <= 0)
        IP_FAIL(ctx, IP_ERR_INVALID_IMAGE, "invalid JPEG dimensions");
    if (ctx->channels != 1 && ctx->channels != 3 && ctx->channels != 4)
        IP_FAIL(ctx, IP_ERR_UNSUPPORTED, "JPEG component count is not supported");

    ctx->bit_depth = 8;
    if (!ip_checked_image_size(ctx->width, ctx->height, ctx->channels, &ctx->decoded_bytes))
        IP_FAIL(ctx, IP_ERR_LIMIT, "decoded image size overflows native size");

    return ip_check_max_dimension_limits(ctx);

fail:
    jpeg_destroy_decompress(&cinfo);
    ctx->jmp_armed = 0;
    return 0;
}

static VALUE ip_inspect_image_entry_body(VALUE ptr) {
    ip_inspect_call_t *call = (ip_inspect_call_t *)ptr;
    ip_context_t *ctx = ip_context_new();
    if (!ctx)
        rb_raise(rb_eImagePackOutOfMemoryError, "failed to allocate native context");
    call->ctx = ctx;

    apply_configuration(call->self, ctx);

    if (!ip_prepare_input_bytes(ctx, call->input, ip_parse_input_kind(call->input_kind)) ||
        !ip_inspect_jpeg_header(ctx, 1)) {
        ip_raise_for_status(ctx);
        rb_raise(rb_eImagePackInvalidImageError, "failed to inspect JPEG image");
    }

    int width = ctx->width;
    int height = ctx->height;
    int channels = ctx->channels;
    int bit_depth = ctx->bit_depth;
    int color_space = ctx->jpeg_color_space;
    size_t decoded_bytes = ctx->decoded_bytes;

    ip_context_free(ctx);
    call->ctx = NULL;

    VALUE hash = rb_hash_new();
    rb_hash_aset(hash, ip_sym("format"), ip_sym("jpeg"));
    rb_hash_aset(hash, ip_sym("width"), INT2NUM(width));
    rb_hash_aset(hash, ip_sym("height"), INT2NUM(height));
    rb_hash_aset(hash, ip_sym("channels"), INT2NUM(channels));
    rb_hash_aset(hash, ip_sym("bit_depth"), INT2NUM(bit_depth));
    rb_hash_aset(hash, ip_sym("color_space"), ip_sym(ip_jpeg_color_space_name(color_space)));
    rb_hash_aset(hash, ip_sym("decoded_bytes"), SIZET2NUM(decoded_bytes));
    return hash;
}

static VALUE ip_inspect_image_entry(VALUE self, VALUE input, VALUE input_kind) {
    ip_inspect_call_t call = {self, input, input_kind, NULL};
    return rb_ensure(ip_inspect_image_entry_body, (VALUE)&call, ip_call_cleanup, (VALUE)&call.ctx);
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
                                                      int mozjpeg_trellis_enabled,
                                                      int scan_opt_enabled, int measurement) {
    if (mozjpeg_size_mode) {
        cinfo->optimize_coding = TRUE;

        if (progressive_requested) {
            if (mozjpeg_trellis_enabled) {
                jpeg_c_set_bool_param(cinfo, JBOOLEAN_USE_SCANS_IN_TRELLIS, TRUE);
                jpeg_c_set_bool_param(cinfo, JBOOLEAN_TRELLIS_EOB_OPT, TRUE);
            }
            jpeg_c_set_int_param(cinfo, JINT_DC_SCAN_OPT_MODE, 2);
            int run_scan_search = (measurement || !scan_opt_enabled) ? FALSE : TRUE;
            jpeg_c_set_bool_param(cinfo, JBOOLEAN_OPTIMIZE_SCANS, run_scan_search);
            jpeg_simple_progression(cinfo);
            jpeg_c_set_bool_param(cinfo, JBOOLEAN_OPTIMIZE_SCANS, run_scan_search);
        } else {
            cinfo->scan_info = NULL;
            cinfo->num_scans = 0;
            jpeg_c_set_bool_param(cinfo, JBOOLEAN_OPTIMIZE_SCANS, FALSE);
        }

        if (!mozjpeg_trellis_enabled)
            ip_disable_mozjpeg_trellis(cinfo);

        return;
    }

    cinfo->optimize_coding = FALSE;
    cinfo->dct_method = IP_FAST_DCT;
    cinfo->smoothing_factor = 0;
    jpeg_c_set_bool_param(cinfo, JBOOLEAN_OPTIMIZE_SCANS, FALSE);
    ip_disable_mozjpeg_trellis(cinfo);

    if (progressive_requested) {
        jpeg_simple_progression(cinfo);
        cinfo->optimize_coding = TRUE;
    }
}

static int encode_pixels_with_libjpeg(ip_context_t *ctx, int mozjpeg_size_mode, int measurement) {
    struct jpeg_compress_struct cinfo;
    ip_jpeg_error_mgr jerr;
    unsigned long jpeg_size = 0;
    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));
    cinfo.err = ip_use_error(&jerr, ctx, ip_jpeg_encode_error_exit);
    ctx->transient_jpeg_buf = NULL;

    ctx->jmp_armed = 1;
    if (setjmp(ctx->jmpbuf)) {
        if (ctx->status == IP_OK)
            ip_context_set_error(ctx, IP_ERR_ENCODE, "JPEG encode failed");
        goto fail;
    }

    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &ctx->transient_jpeg_buf, &jpeg_size);

    cinfo.image_width = (JDIMENSION)ctx->width;
    cinfo.image_height = (JDIMENSION)ctx->height;
    cinfo.input_components = ctx->channels;
    if (ctx->decoded_as_ycbcr && ctx->channels == 3) {
        cinfo.in_color_space = JCS_YCbCr;
    } else {
        cinfo.in_color_space =
            ctx->channels == 4 ? JCS_EXT_RGBA : color_space_for_channels(ctx->channels);
    }

    configure_mozjpeg_profile_before_defaults(&cinfo, mozjpeg_size_mode);
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, ctx->quality, TRUE);
    configure_mozjpeg_features_after_defaults(&cinfo, mozjpeg_size_mode, ctx->progressive,
                                              ctx->mozjpeg_trellis_enabled,
                                              ctx->mozjpeg_scan_opt_enabled, measurement);

    jpeg_start_compress(&cinfo, TRUE);

    if (!ctx->strip_metadata)
        ip_write_preserved_markers(ctx, &cinfo);

    while (cinfo.next_scanline < cinfo.image_height) {
        if (atomic_load(&ctx->cancelled))
            IP_FAIL_GOTO(ctx, IP_ERR_CANCELLED, "JPEG encode cancelled");

        JSAMPROW rows[16];
        JDIMENSION start_scanline = cinfo.next_scanline;
        JDIMENSION batch = cinfo.image_height - start_scanline;
        if (batch > 16)
            batch = 16;

        for (JDIMENSION i = 0; i < batch; i++)
            rows[i] = (JSAMPROW)(ctx->pixel_data + ((size_t)(start_scanline + i) *
                                                    (size_t)ctx->width * (size_t)ctx->channels));

        jpeg_write_scanlines(&cinfo, rows, batch);
    }

    jpeg_finish_compress(&cinfo);

    if (!measurement && ctx->max_output_size > 0 && (size_t)jpeg_size > ctx->max_output_size)
        IP_FAIL_GOTO(ctx, IP_ERR_LIMIT, "output exceeds max_output_size");

    jpeg_destroy_compress(&cinfo);
    ctx->jmp_armed = 0;

    ctx->output_data = ctx->transient_jpeg_buf;
    ctx->transient_jpeg_buf = NULL;
    ctx->output_size = (size_t)jpeg_size;
    ctx->output_owner = IP_OUTPUT_OWNER_MALLOC;
    return 1;

fail:
    jpeg_destroy_compress(&cinfo);
    free(ctx->transient_jpeg_buf);
    ctx->transient_jpeg_buf = NULL;
    ctx->jmp_armed = 0;
    return 0;
}

static int ip_jpeg_decode_to_pixels(ip_context_t *ctx, unsigned char **pixels, int *width,
                                    int *height, int *channels, int fast_decode_mode,
                                    int allow_ycbcr_transcode) {
    struct jpeg_decompress_struct cinfo;
    ip_jpeg_error_mgr jerr;
    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));
    cinfo.err = ip_use_error(&jerr, ctx, ip_jpeg_invalid_error_exit);
    ctx->transient_decode_buf = NULL;
    ctx->source_orientation = 1;
    ctx->decoded_as_ycbcr = 0;

    ctx->jmp_armed = 1;
    if (setjmp(ctx->jmpbuf)) {
        if (ctx->status == IP_OK)
            ip_context_set_error(ctx, IP_ERR_INVALID_IMAGE, "JPEG decode failed");
        goto fail;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, ctx->input_data, (unsigned long)ctx->input_size);

    jpeg_save_markers(&cinfo, JPEG_APP0 + 1, 0xFFFF);
    if (!ctx->strip_metadata) {
        jpeg_save_markers(&cinfo, JPEG_COM, 0xFFFF);
        for (int app = 2; app < 16; app++)
            jpeg_save_markers(&cinfo, JPEG_APP0 + app, 0xFFFF);
    }

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
        IP_FAIL_GOTO(ctx, IP_ERR_INVALID_IMAGE, "invalid JPEG header");

    ctx->source_orientation = ip_read_exif_orientation_from_decompress(&cinfo);

    if (cinfo.image_width > (JDIMENSION)INT_MAX || cinfo.image_height > (JDIMENSION)INT_MAX)
        IP_FAIL_GOTO(ctx, IP_ERR_LIMIT, "JPEG dimensions exceed native int range");

    if (cinfo.num_components == 4 || cinfo.jpeg_color_space == JCS_CMYK ||
        cinfo.jpeg_color_space == JCS_YCCK)
        IP_FAIL_GOTO(ctx, IP_ERR_UNSUPPORTED,
                     "CMYK/YCCK JPEG input is not supported in this release");

    int ch = cinfo.num_components == 1 ? 1 : 3;

    ctx->width = (int)cinfo.image_width;
    ctx->height = (int)cinfo.image_height;
    ctx->channels = ch;
    if (!ip_checked_image_size(ctx->width, ctx->height, ctx->channels, &ctx->decoded_bytes))
        IP_FAIL_GOTO(ctx, IP_ERR_LIMIT, "decoded image size overflows native size");

    validate_limits_for_pixels(ctx);
    if (ctx->status != IP_OK)
        goto fail;

    int use_ycbcr = allow_ycbcr_transcode && ch == 3;
    cinfo.out_color_space = ch == 1 ? JCS_GRAYSCALE : (use_ycbcr ? JCS_YCbCr : JCS_RGB);
    ctx->decoded_as_ycbcr = use_ycbcr;
    if (fast_decode_mode)
        ip_apply_fast_decode(&cinfo);

    jpeg_start_decompress(&cinfo);

    if (!ctx->strip_metadata && !ip_save_markers_from_decompress(ctx, &cinfo))
        goto fail;

    size_t row_stride = 0;
    size_t size = 0;
    if (!ip_checked_mul_size((size_t)cinfo.output_width, (size_t)cinfo.output_components,
                             &row_stride) ||
        !ip_checked_mul_size(row_stride, (size_t)cinfo.output_height, &size))
        IP_FAIL_GOTO(ctx, IP_ERR_LIMIT, "decoded image buffer size overflow");

    ctx->transient_decode_buf = (unsigned char *)malloc(size);
    if (!ctx->transient_decode_buf && size > 0)
        IP_FAIL_GOTO(ctx, IP_ERR_OOM, "failed to allocate decoded pixel buffer");

    while (cinfo.output_scanline < cinfo.output_height) {
        if (atomic_load(&ctx->cancelled))
            IP_FAIL_GOTO(ctx, IP_ERR_CANCELLED, "JPEG decode cancelled");

        JSAMPROW rows[16];
        JDIMENSION batch = cinfo.output_height - cinfo.output_scanline;
        if (batch > 16)
            batch = 16;

        for (JDIMENSION i = 0; i < batch; i++)
            rows[i] =
                ctx->transient_decode_buf + ((size_t)(cinfo.output_scanline + i) * row_stride);

        jpeg_read_scanlines(&cinfo, rows, batch);
    }

    int out_width = (int)cinfo.output_width;
    int out_height = (int)cinfo.output_height;

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    ctx->jmp_armed = 0;

    unsigned char *buf = ctx->transient_decode_buf;
    ctx->transient_decode_buf = NULL;

    if (ctx->strip_metadata && ctx->source_orientation > 1) {
        if (!ip_transform_pixels_for_orientation(ctx, &buf, &out_width, &out_height, ch)) {
            free(buf);
            return 0;
        }

        ctx->width = out_width;
        ctx->height = out_height;
        ctx->channels = ch;
        validate_limits_for_pixels(ctx);
        if (ctx->status != IP_OK) {
            free(buf);
            return 0;
        }
    }

    *pixels = buf;
    *width = out_width;
    *height = out_height;
    *channels = ch;
    return 1;

fail:
    jpeg_destroy_decompress(&cinfo);
    free(ctx->transient_decode_buf);
    ctx->transient_decode_buf = NULL;
    ctx->jmp_armed = 0;
    return 0;
}

static int compress_jpeg_input_with_mode(ip_context_t *ctx, int mozjpeg_size_mode) {
    if (ctx->pixel_data) {
        return encode_pixels_with_libjpeg(ctx, mozjpeg_size_mode, 0);
    }

    unsigned char *pixels = NULL;
    int width = 0;
    int height = 0;
    int channels = 0;
    if (!ip_jpeg_decode_to_pixels(ctx, &pixels, &width, &height, &channels, !mozjpeg_size_mode, 1))
        return 0;

    ctx->owned_pixel_data = pixels;
    ctx->pixel_data = pixels;
    ctx->pixel_size = (size_t)width * (size_t)height * (size_t)channels;
    ctx->width = width;
    ctx->height = height;
    ctx->channels = channels;
    ctx->decoded_bytes = ctx->pixel_size;

    return encode_pixels_with_libjpeg(ctx, mozjpeg_size_mode, 0);
}

static void ip_clear_output_buffer(ip_context_t *ctx) {
    if (!ctx)
        return;

    if (ctx->output_data && ctx->output_owner == IP_OUTPUT_OWNER_MALLOC)
        free(ctx->output_data);

    ctx->output_data = NULL;
    ctx->output_size = 0;
    ctx->output_owner = IP_OUTPUT_OWNER_NONE;
}

static int ip_decode_jpeg_to_luma_buffer(ip_context_t *ctx, const unsigned char *data, size_t size,
                                         unsigned char **luma, int *width, int *height) {
    struct jpeg_decompress_struct cinfo;
    ip_jpeg_error_mgr jerr;
    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));
    cinfo.err = ip_use_error(&jerr, ctx, ip_jpeg_invalid_error_exit);
    ctx->transient_decode_buf = NULL;

    ctx->jmp_armed = 1;
    if (setjmp(ctx->jmpbuf)) {
        if (ctx->status == IP_OK)
            ip_context_set_error(ctx, IP_ERR_INVALID_IMAGE, "JPEG luma decode failed");
        goto fail;
    }

    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, data, (unsigned long)size);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
        IP_FAIL_GOTO(ctx, IP_ERR_INVALID_IMAGE, "invalid JPEG header");

    if (cinfo.image_width > (JDIMENSION)INT_MAX || cinfo.image_height > (JDIMENSION)INT_MAX)
        IP_FAIL_GOTO(ctx, IP_ERR_LIMIT, "JPEG dimensions exceed native int range");

    if (cinfo.num_components == 4 || cinfo.jpeg_color_space == JCS_CMYK ||
        cinfo.jpeg_color_space == JCS_YCCK)
        IP_FAIL_GOTO(ctx, IP_ERR_UNSUPPORTED,
                     "CMYK/YCCK JPEG input is not supported in this release");

    int old_width = ctx->width;
    int old_height = ctx->height;
    int old_channels = ctx->channels;
    size_t old_decoded_bytes = ctx->decoded_bytes;

    ctx->width = (int)cinfo.image_width;
    ctx->height = (int)cinfo.image_height;
    ctx->channels = 1;
    if (!ip_checked_image_size(ctx->width, ctx->height, 1, &ctx->decoded_bytes)) {
        ctx->width = old_width;
        ctx->height = old_height;
        ctx->channels = old_channels;
        ctx->decoded_bytes = old_decoded_bytes;
        IP_FAIL_GOTO(ctx, IP_ERR_LIMIT, "decoded luma buffer size overflows native size");
    }
    validate_limits_for_pixels(ctx);
    ctx->width = old_width;
    ctx->height = old_height;
    ctx->channels = old_channels;
    size_t luma_size = ctx->decoded_bytes;
    ctx->decoded_bytes = old_decoded_bytes;

    if (ctx->status != IP_OK)
        goto fail;

    cinfo.out_color_space = JCS_GRAYSCALE;
    ip_apply_fast_decode(&cinfo);

    jpeg_start_decompress(&cinfo);

    size_t luma_stride = (size_t)cinfo.output_width;
    if (cinfo.output_components != 1 || luma_stride == 0 ||
        luma_size != luma_stride * (size_t)cinfo.output_height)
        IP_FAIL_GOTO(ctx, IP_ERR_LIMIT, "decoded luma buffer size mismatch");

    ctx->transient_decode_buf = (unsigned char *)malloc(luma_size);
    if (!ctx->transient_decode_buf && luma_size > 0)
        IP_FAIL_GOTO(ctx, IP_ERR_OOM, "failed to allocate luma buffer");

    while (cinfo.output_scanline < cinfo.output_height) {
        if (atomic_load(&ctx->cancelled))
            IP_FAIL_GOTO(ctx, IP_ERR_CANCELLED, "JPEG luma decode cancelled");

        JSAMPROW rows[16];
        JDIMENSION batch = cinfo.output_height - cinfo.output_scanline;
        if (batch > 16)
            batch = 16;

        for (JDIMENSION i = 0; i < batch; i++)
            rows[i] =
                ctx->transient_decode_buf + ((size_t)(cinfo.output_scanline + i) * luma_stride);

        jpeg_read_scanlines(&cinfo, rows, batch);
    }

    int out_width = (int)cinfo.output_width;
    int out_height = (int)cinfo.output_height;

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    ctx->jmp_armed = 0;

    *luma = ctx->transient_decode_buf;
    ctx->transient_decode_buf = NULL;
    *width = out_width;
    *height = out_height;
    return 1;

fail:
    jpeg_destroy_decompress(&cinfo);
    free(ctx->transient_decode_buf);
    ctx->transient_decode_buf = NULL;
    ctx->jmp_armed = 0;
    return 0;
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

    const unsigned char *IP_RESTRICT src = pixels;
    unsigned char *IP_RESTRICT dst = luma;

    if (channels == 3) {
        for (size_t i = 0; i < count; i++) {
            unsigned int r = src[i * 3 + 0];
            unsigned int g = src[i * 3 + 1];
            unsigned int b = src[i * 3 + 2];
            dst[i] = (unsigned char)((19595u * r + 38470u * g + 7471u * b + 32768u) >> 16);
        }
        return luma;
    }

    for (size_t i = 0; i < count; i++) {
        unsigned int r = src[i * 4 + 0];
        unsigned int g = src[i * 4 + 1];
        unsigned int b = src[i * 4 + 2];
        dst[i] = (unsigned char)((19595u * r + 38470u * g + 7471u * b + 32768u) >> 16);
    }
    return luma;
}

static double ip_ssim_window_score_double(int32_t n, int32_t sum_a, int32_t sum_b, int32_t sum_a2,
                                          int32_t sum_b2, int32_t sum_ab) {
    const double c1 = 6.5025;
    const double c2 = 58.5225;

    double inv_n = 1.0 / (double)n;
    double mean_a = (double)sum_a * inv_n;
    double mean_b = (double)sum_b * inv_n;
    double var_a = ((double)sum_a2 * inv_n) - (mean_a * mean_a);
    double var_b = ((double)sum_b2 * inv_n) - (mean_b * mean_b);
    double cov_ab = ((double)sum_ab * inv_n) - (mean_a * mean_b);

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
    return ssim;
}

static inline double ip_ssim_window_8x8(const unsigned char *IP_RESTRICT a,
                                        const unsigned char *IP_RESTRICT b, int width, int x0,
                                        int y0) {
    int32_t sum_a = 0, sum_b = 0, sum_a2 = 0, sum_b2 = 0, sum_ab = 0;

    for (int y = 0; y < 8; y++) {
        const unsigned char *pa = a + (size_t)(y0 + y) * (size_t)width + (size_t)x0;
        const unsigned char *pb = b + (size_t)(y0 + y) * (size_t)width + (size_t)x0;
        for (int x = 0; x < 8; x++) {
            int32_t la = pa[x];
            int32_t lb = pb[x];
            sum_a += la;
            sum_b += lb;
            sum_a2 += la * la;
            sum_b2 += lb * lb;
            sum_ab += la * lb;
        }
    }

    return ip_ssim_window_score_double(64, sum_a, sum_b, sum_a2, sum_b2, sum_ab);
}

static inline double ip_ssim_window_var(const unsigned char *IP_RESTRICT a,
                                        const unsigned char *IP_RESTRICT b, int width, int x0,
                                        int y0, int x1, int y1) {
    int32_t sum_a = 0, sum_b = 0, sum_a2 = 0, sum_b2 = 0, sum_ab = 0;
    int32_t n = 0;

    for (int y = y0; y < y1; y++) {
        const unsigned char *pa = a + (size_t)y * (size_t)width;
        const unsigned char *pb = b + (size_t)y * (size_t)width;
        for (int x = x0; x < x1; x++) {
            int32_t la = pa[x];
            int32_t lb = pb[x];
            sum_a += la;
            sum_b += lb;
            sum_a2 += la * la;
            sum_b2 += lb * lb;
            sum_ab += la * lb;
            n++;
        }
    }

    if (n <= 0)
        return 1.0;
    return ip_ssim_window_score_double(n, sum_a, sum_b, sum_a2, sum_b2, sum_ab);
}

static double ip_compute_ssim_luma_buffer(const unsigned char *a, const unsigned char *b, int width,
                                          int height) {
    const int window = 8;
    double total_ssim = 0.0;
    int windows = 0;

    int full_x = width / window;
    int full_y = height / window;
    int rem_x = width - full_x * window;
    int rem_y = height - full_y * window;

    for (int by = 0; by < full_y; by++) {
        int y0 = by * window;
        for (int bx = 0; bx < full_x; bx++) {
            int x0 = bx * window;
            total_ssim += ip_ssim_window_8x8(a, b, width, x0, y0);
            windows++;
        }
    }

    if (rem_x > 0) {
        int x0 = full_x * window;
        int x1 = width;
        for (int by = 0; by < full_y; by++) {
            int y0 = by * window;
            int y1 = y0 + window;
            total_ssim += ip_ssim_window_var(a, b, width, x0, y0, x1, y1);
            windows++;
        }
    }

    if (rem_y > 0) {
        int y0 = full_y * window;
        int y1 = height;
        for (int bx = 0; bx < full_x; bx++) {
            int x0 = bx * window;
            int x1 = x0 + window;
            total_ssim += ip_ssim_window_var(a, b, width, x0, y0, x1, y1);
            windows++;
        }
    }

    if (rem_x > 0 && rem_y > 0) {
        int x0 = full_x * window;
        int y0 = full_y * window;
        total_ssim += ip_ssim_window_var(a, b, width, x0, y0, width, height);
        windows++;
    }

    return windows > 0 ? total_ssim / (double)windows : 0.0;
}

static int ip_guard_score_quality(ip_context_t *ctx, int mozjpeg_size_mode, int measurement,
                                  const unsigned char *reference_luma, int reference_width,
                                  int reference_height, unsigned char **out_jpeg,
                                  size_t *out_jpeg_size, double *out_ssim) {
    *out_jpeg = NULL;
    *out_jpeg_size = 0;
    *out_ssim = 0.0;

    ip_clear_output_buffer(ctx);
    if (!encode_pixels_with_libjpeg(ctx, mozjpeg_size_mode, measurement))
        return 0;

    unsigned char *candidate_jpeg = ctx->output_data;
    size_t candidate_jpeg_size = ctx->output_size;
    ctx->output_data = NULL;
    ctx->output_size = 0;
    ctx->output_owner = IP_OUTPUT_OWNER_NONE;

    unsigned char *candidate_luma = NULL;
    int candidate_width = 0;
    int candidate_height = 0;
    if (!ip_decode_jpeg_to_luma_buffer(ctx, candidate_jpeg, candidate_jpeg_size, &candidate_luma,
                                       &candidate_width, &candidate_height)) {
        free(candidate_jpeg);
        return 0;
    }

    if (candidate_width != reference_width || candidate_height != reference_height) {
        free(candidate_luma);
        free(candidate_jpeg);
        ip_context_set_error(ctx, IP_ERR_ENCODE,
                             "candidate JPEG dimensions differ from reference image");
        return 0;
    }

    *out_ssim = ip_compute_ssim_luma_buffer(reference_luma, candidate_luma, reference_width,
                                            reference_height);
    free(candidate_luma);
    *out_jpeg = candidate_jpeg;
    *out_jpeg_size = candidate_jpeg_size;
    return 1;
}

static int guarded_compress_jpeg_input_with_mode(ip_context_t *ctx, int mozjpeg_size_mode) {
    unsigned char *reference_pixels = NULL;
    int reference_width = 0;
    int reference_height = 0;
    int reference_channels = 0;

    if (ctx->pixel_data) {
        reference_pixels = (unsigned char *)ctx->pixel_data;
        reference_width = ctx->width;
        reference_height = ctx->height;
        reference_channels = ctx->channels;
    } else {
        if (!ip_jpeg_decode_to_pixels(ctx, &reference_pixels, &reference_width, &reference_height,
                                      &reference_channels, 1, 0)) {
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
        ip_context_set_error(ctx, IP_ERR_UNSUPPORTED, "min_ssim is not supported for RGBA input");
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
    int probe_measurement = mozjpeg_size_mode ? 1 : 0;
    int have_lo = 0, have_hi = 0;
    int q_lo = 0, q_hi = 0;
    double s_lo = 0.0, s_hi = 0.0;

    while (search_low <= search_high) {
        if (atomic_load(&ctx->cancelled)) {
            free(reference_luma);
            free(best_jpeg);
            ip_context_set_error(ctx, IP_ERR_CANCELLED, "SSIM-guarded JPEG encode cancelled");
            return 0;
        }

        int trial_quality;
        if (have_lo && have_hi && s_hi > s_lo) {
            double t = (ctx->min_ssim - s_lo) / (s_hi - s_lo);
            double q_est = (double)q_lo + t * (double)(q_hi - q_lo);
            trial_quality = (int)(q_est + 0.5);
            if (trial_quality < search_low)
                trial_quality = search_low;
            if (trial_quality > search_high)
                trial_quality = search_high;
        } else {
            trial_quality = search_low + ((search_high - search_low) / 2);
        }

        ctx->quality = trial_quality;

        unsigned char *candidate_jpeg = NULL;
        size_t candidate_jpeg_size = 0;
        double ssim = 0.0;
        if (!ip_guard_score_quality(ctx, mozjpeg_size_mode, probe_measurement, reference_luma,
                                    reference_width, reference_height, &candidate_jpeg,
                                    &candidate_jpeg_size, &ssim)) {
            free(reference_luma);
            free(best_jpeg);
            return 0;
        }

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
            q_hi = trial_quality;
            s_hi = ssim;
            have_hi = 1;
            search_high = trial_quality - 1;
        } else {
            free(candidate_jpeg);
            q_lo = trial_quality;
            s_lo = ssim;
            have_lo = 1;
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

    if (!mozjpeg_size_mode) {
        ctx->quality = best_quality;
        ctx->selected_quality = best_quality;
        ctx->measured_ssim = best_ssim;
        free(reference_luma);
        ctx->output_data = best_jpeg;
        ctx->output_size = best_jpeg_size;
        ctx->output_owner = IP_OUTPUT_OWNER_MALLOC;
        return 1;
    }

    free(best_jpeg);
    best_jpeg = NULL;

    int final_quality = best_quality;
    unsigned char *final_jpeg = NULL;
    size_t final_jpeg_size = 0;
    double final_ssim = 0.0;
    int satisfied = 0;

    while (final_quality <= 100) {
        if (atomic_load(&ctx->cancelled)) {
            free(final_jpeg);
            free(reference_luma);
            ip_context_set_error(ctx, IP_ERR_CANCELLED, "SSIM-guarded JPEG encode cancelled");
            return 0;
        }

        ctx->quality = final_quality;
        unsigned char *jpeg = NULL;
        size_t jpeg_size = 0;
        double ssim = 0.0;
        if (!ip_guard_score_quality(ctx, mozjpeg_size_mode, 0, reference_luma, reference_width,
                                    reference_height, &jpeg, &jpeg_size, &ssim)) {
            free(final_jpeg);
            free(reference_luma);
            return 0;
        }

        free(final_jpeg);
        final_jpeg = jpeg;
        final_jpeg_size = jpeg_size;
        final_ssim = ssim;

        if (ssim >= ctx->min_ssim) {
            satisfied = 1;
            break;
        }
        final_quality++;
    }

    if (!satisfied) {
        char message[512];
        snprintf(message, sizeof(message),
                 "cannot satisfy min_ssim=%.6f; best full-profile SSIM %.6f at quality=100",
                 ctx->min_ssim, final_ssim);
        free(final_jpeg);
        free(reference_luma);
        ip_context_set_error(ctx, IP_ERR_QUALITY, message);
        return 0;
    }

    free(reference_luma);

    ctx->quality = final_quality;
    ctx->selected_quality = final_quality;
    ctx->measured_ssim = final_ssim;
    ctx->output_data = final_jpeg;
    ctx->output_size = final_jpeg_size;
    ctx->output_owner = IP_OUTPUT_OWNER_MALLOC;
    return 1;
}

static void ip_setup_marker_saving(struct jpeg_decompress_struct *cinfo, int strip_metadata) {
    jpeg_save_markers(cinfo, JPEG_APP0 + 1, 0xFFFF);
    if (!strip_metadata) {
        jpeg_save_markers(cinfo, JPEG_COM, 0xFFFF);
        for (int app = 2; app < 16; app++) {
            jpeg_save_markers(cinfo, JPEG_APP0 + app, 0xFFFF);
        }
    }
}

static int ip_validate_lossless_optimize_header(ip_context_t *ctx,
                                                struct jpeg_decompress_struct *srcinfo) {
    if (srcinfo->image_width > (JDIMENSION)INT_MAX || srcinfo->image_height > (JDIMENSION)INT_MAX) {
        ip_context_set_error(ctx, IP_ERR_LIMIT, "JPEG dimensions exceed native int range");
        return 0;
    }

    ctx->width = (int)srcinfo->image_width;
    ctx->height = (int)srcinfo->image_height;
    ctx->channels = srcinfo->num_components;
    ctx->bit_depth = 8;

    if (ctx->max_width < 0 || ctx->max_height < 0) {
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "max_width/max_height must be >= 0");
        return 0;
    }
    return ip_check_max_dimension_limits(ctx);
}

static int ip_lossless_optimize_jpeg(ip_context_t *ctx) {
    struct jpeg_decompress_struct srcinfo;
    struct jpeg_compress_struct dstinfo;
    ip_jpeg_error_mgr srcerr;
    ip_jpeg_error_mgr dsterr;
    jvirt_barray_ptr *coef_arrays = NULL;
    unsigned long jpeg_size = 0;

    memset(&srcinfo, 0, sizeof(srcinfo));
    memset(&dstinfo, 0, sizeof(dstinfo));
    memset(&srcerr, 0, sizeof(srcerr));
    memset(&dsterr, 0, sizeof(dsterr));
    ctx->transient_jpeg_buf = NULL;

    srcinfo.err = ip_use_error(&srcerr, ctx, ip_jpeg_invalid_error_exit);
    dstinfo.err = ip_use_error(&dsterr, ctx, ip_jpeg_encode_error_exit);

    ctx->jmp_armed = 1;
    if (setjmp(ctx->jmpbuf)) {
        if (ctx->status == IP_OK)
            ip_context_set_error(ctx, IP_ERR_ENCODE, "lossless JPEG optimize failed");
        goto fail;
    }

    jpeg_create_decompress(&srcinfo);
    jpeg_create_compress(&dstinfo);
    jpeg_mem_src(&srcinfo, ctx->input_data, (unsigned long)ctx->input_size);
    ip_setup_marker_saving(&srcinfo, ctx->strip_metadata);

    if (atomic_load(&ctx->cancelled))
        IP_FAIL_GOTO(ctx, IP_ERR_CANCELLED, "lossless JPEG optimize cancelled");

    if (jpeg_read_header(&srcinfo, TRUE) != JPEG_HEADER_OK)
        IP_FAIL_GOTO(ctx, IP_ERR_INVALID_IMAGE, "invalid JPEG header");

    ctx->source_orientation = ip_read_exif_orientation_from_decompress(&srcinfo);
    if (ctx->strip_metadata && ctx->source_orientation > 1)
        IP_FAIL_GOTO(ctx, IP_ERR_UNSUPPORTED,
                     "lossless optimize cannot strip EXIF Orientation without changing "
                     "visual orientation; use strip_metadata: false or ImagePack.compress");

    if (!ip_validate_lossless_optimize_header(ctx, &srcinfo))
        goto fail;

    if (!ctx->strip_metadata && !ip_save_markers_from_decompress(ctx, &srcinfo))
        goto fail;

    coef_arrays = jpeg_read_coefficients(&srcinfo);
    jpeg_copy_critical_parameters(&srcinfo, &dstinfo);
    dstinfo.optimize_coding = TRUE;
    dstinfo.num_scans = 0;
    dstinfo.scan_info = NULL;
    if (ctx->progressive) {
        jpeg_simple_progression(&dstinfo);
        dstinfo.optimize_coding = TRUE;
    }

    jpeg_mem_dest(&dstinfo, &ctx->transient_jpeg_buf, &jpeg_size);

    if (atomic_load(&ctx->cancelled))
        IP_FAIL_GOTO(ctx, IP_ERR_CANCELLED, "lossless JPEG optimize cancelled");

    jpeg_write_coefficients(&dstinfo, coef_arrays);
    if (!ctx->strip_metadata)
        ip_write_preserved_markers(ctx, &dstinfo);

    jpeg_finish_compress(&dstinfo);
    jpeg_finish_decompress(&srcinfo);

    if (ctx->max_output_size > 0 && (size_t)jpeg_size > ctx->max_output_size)
        IP_FAIL_GOTO(ctx, IP_ERR_LIMIT, "output exceeds max_output_size");

    jpeg_destroy_compress(&dstinfo);
    jpeg_destroy_decompress(&srcinfo);
    ctx->jmp_armed = 0;

    ctx->output_data = ctx->transient_jpeg_buf;
    ctx->transient_jpeg_buf = NULL;
    ctx->output_size = (size_t)jpeg_size;
    ctx->output_owner = IP_OUTPUT_OWNER_MALLOC;
    return 1;

fail:
    jpeg_destroy_compress(&dstinfo);
    jpeg_destroy_decompress(&srcinfo);
    free(ctx->transient_jpeg_buf);
    ctx->transient_jpeg_buf = NULL;
    ctx->jmp_armed = 0;
    return 0;
}

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

static ip_execution_t ip_async_execution(const ip_context_t *ctx) {
#if IMAGE_PACK_HAS_OFFLOAD_SAFE
    return (ip_offload_runtime_enabled && ctx->has_scheduler) ? IP_EXEC_OFFLOAD : IP_EXEC_NOGVL;
#else
    (void)ctx;
    return IP_EXEC_NOGVL;
#endif
}

static void ip_resolve_execution(ip_context_t *ctx) {
    if (ctx->requested_execution != IP_EXEC_AUTO) {
        ctx->resolved_execution = ctx->requested_execution;
        return;
    }

    if (ctx->cancellable_requested) {
        ctx->resolved_execution = ip_async_execution(ctx);
        return;
    }

    if (ctx->ssim_guard_enabled) {
        ctx->resolved_execution = ip_async_execution(ctx);
        return;
    }

    if (ctx->input_size < ctx->direct_input_threshold &&
        ctx->decoded_bytes < ctx->direct_pixel_threshold) {
        ctx->resolved_execution = IP_EXEC_DIRECT;
        return;
    }

    ctx->resolved_execution = ip_async_execution(ctx);
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
#if IMAGE_PACK_HAS_OFFLOAD_SAFE
        rb_nogvl(ip_run_encode_nogvl, ctx, ip_unblock_function, ctx, RB_NOGVL_OFFLOAD_SAFE);
#else
        ip_context_set_error(ctx, IP_ERR_UNSUPPORTED,
                             "offload execution is unavailable in this runtime; it requires Ruby "
                             ">= 3.4 and IMAGE_PACK_DISABLE_OFFLOAD must not be set");
#endif
    } else {
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "invalid resolved execution mode");
    }

    if (ctx->resolved_execution != IP_EXEC_DIRECT)
        rb_thread_check_ints();

    return ctx->status == IP_OK;
}

static void *ip_run_optimize_nogvl(void *data) {
    ip_context_t *ctx = (ip_context_t *)data;
    if (ctx->status == IP_OK)
        ip_lossless_optimize_jpeg(ctx);
    return NULL;
}

static int ip_run_optimize_context(ip_context_t *ctx) {
    ip_resolve_execution(ctx);

    if (ctx->resolved_execution == IP_EXEC_DIRECT) {
        ip_run_optimize_nogvl(ctx);
    } else if (ctx->resolved_execution == IP_EXEC_NOGVL) {
        rb_nogvl(ip_run_optimize_nogvl, ctx, ip_unblock_function, ctx, 0);
    } else if (ctx->resolved_execution == IP_EXEC_OFFLOAD) {
#if IMAGE_PACK_HAS_OFFLOAD_SAFE
        rb_nogvl(ip_run_optimize_nogvl, ctx, ip_unblock_function, ctx, RB_NOGVL_OFFLOAD_SAFE);
#else
        ip_context_set_error(ctx, IP_ERR_UNSUPPORTED,
                             "offload execution is unavailable in this runtime; it requires Ruby "
                             ">= 3.4 and IMAGE_PACK_DISABLE_OFFLOAD must not be set");
#endif
    } else {
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "invalid resolved execution mode");
    }

    if (ctx->resolved_execution != IP_EXEC_DIRECT)
        rb_thread_check_ints();

    return ctx->status == IP_OK;
}

static size_t config_size_value(VALUE config, ID id, size_t fallback, const char *name) {
    VALUE value = rb_funcall(config, id, 0);
    if (NIL_P(value))
        return fallback;
    if (!RB_INTEGER_TYPE_P(value) || ip_value_negative(value)) {
        rb_raise(rb_eImagePackInvalidArgumentError, "%s must be an Integer >= 0", name);
    }
    return NUM2SIZET(value);
}

static int config_int_value(VALUE config, ID id, int fallback, const char *name) {
    VALUE value = rb_funcall(config, id, 0);
    if (NIL_P(value))
        return fallback;
    if (!RB_INTEGER_TYPE_P(value) || ip_value_negative(value)) {
        rb_raise(rb_eImagePackInvalidArgumentError, "%s must be an Integer >= 0", name);
    }
    return NUM2INT(value);
}

static void apply_configuration(VALUE self, ip_context_t *ctx) {
    VALUE config = rb_funcall(self, id_configuration, 0);
    ctx->direct_input_threshold = config_size_value(
        config, id_direct_input_threshold, ctx->direct_input_threshold, "direct_input_threshold");
    ctx->direct_pixel_threshold = config_size_value(
        config, id_direct_pixel_threshold, ctx->direct_pixel_threshold, "direct_pixel_threshold");
    ctx->max_pixels =
        (uint64_t)config_size_value(config, id_max_pixels, (size_t)ctx->max_pixels, "max_pixels");
    ctx->max_width = config_int_value(config, id_max_width, ctx->max_width, "max_width");
    ctx->max_height = config_int_value(config, id_max_height, ctx->max_height, "max_height");
    ctx->max_output_size =
        config_size_value(config, id_max_output_size, ctx->max_output_size, "max_output_size");
    ctx->max_input_size =
        config_size_value(config, id_max_input_size, ctx->max_input_size, "max_input_size");
}

static void validate_limits_for_pixels(ip_context_t *ctx) {
    size_t decoded_bytes = 0;

    if (ctx->width <= 0 || ctx->height <= 0 || ctx->channels <= 0) {
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "image dimensions must be positive");
        return;
    }
    if (ctx->max_width < 0 || ctx->max_height < 0) {
        ip_context_set_error(ctx, IP_ERR_INVALID_ARGUMENT, "max_width/max_height must be >= 0");
        return;
    }
    if (!ip_check_max_dimension_limits(ctx))
        return;
    if (!ip_checked_image_size(ctx->width, ctx->height, ctx->channels, &decoded_bytes)) {
        ip_context_set_error(ctx, IP_ERR_LIMIT, "decoded image size overflows native size");
        return;
    }

    ctx->decoded_bytes = decoded_bytes;
}

static VALUE ip_compress_jpeg_entry_body(VALUE ptr) {
    ip_compress_jpeg_call_t *call = (ip_compress_jpeg_call_t *)ptr;
    ip_context_t *ctx = ip_context_new();
    if (!ctx)
        rb_raise(rb_eImagePackOutOfMemoryError, "failed to allocate native context");
    call->ctx = ctx;

    ip_output_kind_t out_kind = ip_parse_output_kind(call->output_kind);
    ctx->algo = ip_parse_algo(call->algo);
    ctx->quality = NUM2INT(call->quality);
    ctx->selected_quality = ctx->quality;
    ip_validate_quality_or_raise(ctx);
    ctx->min_ssim = NUM2DBL(call->min_ssim);
    ctx->ssim_guard_enabled = ctx->min_ssim > 0.0;
    ip_validate_min_ssim_or_raise(ctx);
    ctx->mozjpeg_trellis_enabled = ip_bool_value(call->mozjpeg_trellis);
    ctx->mozjpeg_scan_opt_enabled = ip_bool_value(call->mozjpeg_scan_opt);
    ctx->progressive = ip_bool_value(call->progressive);
    ctx->strip_metadata = ip_bool_value(call->strip_metadata);
    ctx->requested_execution = ip_parse_execution(call->execution);
    ctx->cancellable_requested = ip_bool_value(call->cancellable);
    ctx->has_scheduler = ip_bool_value(call->has_scheduler);
    ctx->strict = ip_bool_value(call->strict);
    apply_configuration(call->self, ctx);

    ip_input_kind_t in_kind = ip_parse_input_kind(call->input_kind);
    if (!ip_prepare_output_path(ctx, call->output, out_kind) ||
        !ip_prepare_input_bytes(ctx, call->input, in_kind)) {
        ip_raise_for_status(ctx);
        rb_raise(rb_eImagePackInvalidArgumentError, "invalid JPEG input");
    }

    if (ctx->requested_execution == IP_EXEC_AUTO && ctx->input_size < ctx->direct_input_threshold &&
        !ip_inspect_jpeg_header(ctx, 0)) {
        ip_raise_for_status(ctx);
        rb_raise(rb_eImagePackInvalidImageError, "invalid JPEG input");
    }

    ip_resolve_execution(ctx);
    if (!ip_ensure_owned_input_for_async(ctx, call->input, in_kind)) {
        ip_raise_for_status(ctx);
        rb_raise(rb_eImagePackInvalidArgumentError, "invalid JPEG input");
    }

    ip_run_context(ctx);
    if (ip_bool_value(call->report)) {
        VALUE output_value = ip_finish_output(ctx, out_kind);
        return ip_build_report(ctx, output_value);
    }
    return ip_finish_output(ctx, out_kind);
}

static VALUE ip_compress_jpeg_entry(int argc, VALUE *argv, VALUE self) {
    rb_check_arity(argc, 16, 16);
    ip_compress_jpeg_call_t call = {self,     argv[0],  argv[1],  argv[2],  argv[3],  argv[4],
                                    argv[5],  argv[6],  argv[7],  argv[8],  argv[9],  argv[10],
                                    argv[11], argv[12], argv[13], argv[14], argv[15], NULL};
    return rb_ensure(ip_compress_jpeg_entry_body, (VALUE)&call, ip_call_cleanup, (VALUE)&call.ctx);
}

static VALUE ip_compress_pixels_entry_body(VALUE ptr) {
    ip_compress_pixels_call_t *call = (ip_compress_pixels_call_t *)ptr;
    ip_context_t *ctx = ip_context_new();
    if (!ctx)
        rb_raise(rb_eImagePackOutOfMemoryError, "failed to allocate native context");
    call->ctx = ctx;

    ip_output_kind_t out_kind = ip_parse_output_kind(call->output_kind);
    ctx->algo = ip_parse_algo(call->algo);
    ctx->quality = NUM2INT(call->quality);
    ctx->selected_quality = ctx->quality;
    ip_validate_quality_or_raise(ctx);
    ctx->min_ssim = NUM2DBL(call->min_ssim);
    ctx->ssim_guard_enabled = ctx->min_ssim > 0.0;
    ip_validate_min_ssim_or_raise(ctx);
    ctx->mozjpeg_trellis_enabled = ip_bool_value(call->mozjpeg_trellis);
    ctx->mozjpeg_scan_opt_enabled = ip_bool_value(call->mozjpeg_scan_opt);
    ctx->progressive = ip_bool_value(call->progressive);
    ctx->strip_metadata = 1;
    ctx->requested_execution = ip_parse_execution(call->execution);
    ctx->cancellable_requested = ip_bool_value(call->cancellable);
    ctx->has_scheduler = ip_bool_value(call->has_scheduler);
    ctx->strict = ip_bool_value(call->strict);
    apply_configuration(call->self, ctx);

    if (!ip_prepare_output_path(ctx, call->output, out_kind) ||
        !ip_prepare_pixels(ctx, call->buffer, NUM2INT(call->width), NUM2INT(call->height),
                           NUM2INT(call->channels), ip_bool_value(call->exact_size))) {
        ip_raise_for_status(ctx);
        rb_raise(rb_eImagePackInvalidArgumentError, "invalid pixel input");
    }

    validate_limits_for_pixels(ctx);
    if (ctx->status != IP_OK)
        ip_raise_for_status(ctx);

    ip_resolve_execution(ctx);
    if (!ip_ensure_owned_pixels_for_async(ctx, call->buffer)) {
        ip_raise_for_status(ctx);
        rb_raise(rb_eImagePackInvalidArgumentError, "invalid pixel input");
    }

    ip_run_context(ctx);
    if (ip_bool_value(call->report)) {
        VALUE output_value = ip_finish_output(ctx, out_kind);
        return ip_build_report(ctx, output_value);
    }
    return ip_finish_output(ctx, out_kind);
}

static VALUE ip_compress_pixels_entry(int argc, VALUE *argv, VALUE self) {
    rb_check_arity(argc, 18, 18);
    ip_compress_pixels_call_t call = {self,     argv[0],  argv[1],  argv[2],  argv[3],
                                      argv[4],  argv[5],  argv[6],  argv[7],  argv[8],
                                      argv[9],  argv[10], argv[11], argv[12], argv[13],
                                      argv[14], argv[15], argv[16], argv[17], NULL};
    return rb_ensure(ip_compress_pixels_entry_body, (VALUE)&call, ip_call_cleanup,
                     (VALUE)&call.ctx);
}

static VALUE ip_optimize_jpeg_entry_body(VALUE ptr) {
    ip_optimize_jpeg_call_t *call = (ip_optimize_jpeg_call_t *)ptr;
    ip_context_t *ctx = ip_context_new();
    if (!ctx)
        rb_raise(rb_eImagePackOutOfMemoryError, "failed to allocate native context");
    call->ctx = ctx;

    ip_output_kind_t out_kind = ip_parse_output_kind(call->output_kind);
    ctx->progressive = ip_bool_value(call->progressive);
    ctx->strip_metadata = ip_bool_value(call->strip_metadata);
    ctx->requested_execution = ip_parse_execution(call->execution);
    ctx->cancellable_requested = ip_bool_value(call->cancellable);
    ctx->has_scheduler = ip_bool_value(call->has_scheduler);
    ctx->strict = ip_bool_value(call->strict);
    ctx->ssim_guard_enabled = 0;
    apply_configuration(call->self, ctx);

    ip_input_kind_t in_kind = ip_parse_input_kind(call->input_kind);
    if (!ip_prepare_output_path(ctx, call->output, out_kind) ||
        !ip_prepare_input_bytes(ctx, call->input, in_kind)) {
        ip_raise_for_status(ctx);
        rb_raise(rb_eImagePackInvalidArgumentError, "invalid JPEG input");
    }

    if (ctx->requested_execution == IP_EXEC_AUTO && ctx->input_size < ctx->direct_input_threshold &&
        !ip_inspect_jpeg_header(ctx, 1)) {
        ip_raise_for_status(ctx);
        rb_raise(rb_eImagePackInvalidImageError, "invalid JPEG input");
    }

    ip_resolve_execution(ctx);
    if (!ip_ensure_owned_input_for_async(ctx, call->input, in_kind)) {
        ip_raise_for_status(ctx);
        rb_raise(rb_eImagePackInvalidArgumentError, "invalid JPEG input");
    }

    ip_run_optimize_context(ctx);
    return ip_finish_output(ctx, out_kind);
}

static VALUE ip_optimize_jpeg_entry(VALUE self, VALUE input, VALUE input_kind, VALUE output,
                                    VALUE output_kind, VALUE progressive, VALUE strip_metadata,
                                    VALUE execution, VALUE cancellable, VALUE has_scheduler,
                                    VALUE strict) {
    ip_optimize_jpeg_call_t call = {self,        input,         input_kind,     output,
                                    output_kind, progressive,   strip_metadata, execution,
                                    cancellable, has_scheduler, strict,         NULL};
    return rb_ensure(ip_optimize_jpeg_entry_body, (VALUE)&call, ip_call_cleanup, (VALUE)&call.ctx);
}

IMAGE_PACK_INIT_EXPORT void Init_image_pack(void) {
    static const struct {
        ID *slot;
        const char *name;
    } ids[] = {{&id_jpeg_turbo, "jpeg_turbo"},
               {&id_mozjpeg, "mozjpeg"},
               {&id_direct, "direct"},
               {&id_nogvl, "nogvl"},
               {&id_offload, "offload"},
               {&id_auto, "auto"},
               {&id_bytes, "bytes"},
               {&id_path, "path"},
               {&id_io_buffer, "io_buffer"},
               {&id_return_string, "return_string"},
               {&id_configuration, "configuration"},
               {&id_direct_input_threshold, "direct_input_threshold"},
               {&id_direct_pixel_threshold, "direct_pixel_threshold"},
               {&id_max_pixels, "max_pixels"},
               {&id_max_width, "max_width"},
               {&id_max_height, "max_height"},
               {&id_max_output_size, "max_output_size"},
               {&id_max_input_size, "max_input_size"}};
    for (size_t i = 0; i < IP_ARRAY_LEN(ids); i++)
        *ids[i].slot = rb_intern(ids[i].name);

    rb_mImagePack = rb_define_module("ImagePack");

    static const struct {
        VALUE *slot;
        const char *name;
    } exceptions[] = {{&rb_eImagePackError, "Error"},
                      {&rb_eImagePackInvalidArgumentError, "InvalidArgumentError"},
                      {&rb_eImagePackInvalidImageError, "InvalidImageError"},
                      {&rb_eImagePackUnsupportedError, "UnsupportedError"},
                      {&rb_eImagePackLimitExceededError, "LimitExceededError"},
                      {&rb_eImagePackEncodeError, "EncodeError"},
                      {&rb_eImagePackQualityConstraintError, "QualityConstraintError"},
                      {&rb_eImagePackOutOfMemoryError, "OutOfMemoryError"},
                      {&rb_eImagePackCancelledError, "CancelledError"}};
    for (size_t i = 0; i < IP_ARRAY_LEN(exceptions); i++)
        *exceptions[i].slot = rb_const_get(rb_mImagePack, rb_intern(exceptions[i].name));

    rb_define_const(rb_mImagePack, "NATIVE_MOZJPEG_VERSION",
                    rb_str_new_cstr(IMAGE_PACK_MOZJPEG_VERSION));
#if defined(IMAGE_PACK_HAS_SIMD)
    rb_define_const(rb_mImagePack, "NATIVE_SIMD", Qtrue);
#else
    rb_define_const(rb_mImagePack, "NATIVE_SIMD", Qfalse);
#endif
    ip_offload_runtime_enabled = (getenv("IMAGE_PACK_DISABLE_OFFLOAD") == NULL) ? 1 : 0;
#if IMAGE_PACK_HAS_OFFLOAD_SAFE
    rb_define_const(rb_mImagePack, "NATIVE_OFFLOAD_SAFE",
                    ip_offload_runtime_enabled ? Qtrue : Qfalse);
#else
    rb_define_const(rb_mImagePack, "NATIVE_OFFLOAD_SAFE", Qfalse);
#endif

    static const struct {
        const char *name;
        VALUE (*fn)(ANYARGS);
        int arity;
    } methods[] = {{"__compress_jpeg", (VALUE (*)(ANYARGS))ip_compress_jpeg_entry, -1},
                   {"__compress_pixels", (VALUE (*)(ANYARGS))ip_compress_pixels_entry, -1},
                   {"__optimize_jpeg", (VALUE (*)(ANYARGS))ip_optimize_jpeg_entry, 10},
                   {"__inspect_image", (VALUE (*)(ANYARGS))ip_inspect_image_entry, 2}};
    for (size_t i = 0; i < IP_ARRAY_LEN(methods); i++) {
        rb_define_singleton_method(rb_mImagePack, methods[i].name, methods[i].fn, methods[i].arity);
        rb_funcall(rb_mImagePack, rb_intern("private_class_method"), 1,
                   ID2SYM(rb_intern(methods[i].name)));
    }
}
