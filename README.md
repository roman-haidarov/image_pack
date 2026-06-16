# image_pack

`image_pack` is a Ruby-native JPEG compressor prototype for Ruby `>= 3.4.0`.

Current version: `0.2.2`.

This pure-C variant intentionally removes Jpegli and any C++ toolchain requirement.
The native layer is written in C and links against vendored MozJPEG, which is libjpeg-compatible and based on libjpeg-turbo.

## Backends / modes

```ruby
ImagePack.compress(input, algo: :jpeg_turbo)
ImagePack.compress(input, algo: :mozjpeg)
```

- `:jpeg_turbo` / `:fast` — fast libjpeg-compatible mode with MozJPEG-specific size optimizations disabled.
- `:mozjpeg` / `:size` — size-oriented mode using optimized Huffman coding and optional progressive output through the MozJPEG/libjpeg API.

Both modes produce ordinary `.jpg` files.

Important implementation note: this prototype uses one pure-C vendored codec family to avoid static-link symbol conflicts between separate libjpeg-compatible libraries. If later we need exact latest `libjpeg-turbo` and exact MozJPEG in one gem, the correct next step is linker isolation / separate native extensions with hidden symbols.


## Native extension layout

For the first prototype the native layer intentionally lives in one file:

```text
ext/image_pack/
├── extconf.rb
└── image_pack.c
```

This mirrors the early `multi_compress` style: one translation unit is easier to expand, debug and refactor while the API and codec boundaries are still moving. The code can be split into modules later, after the first working version is stable.

## API

```ruby
require "image_pack"

jpeg = File.binread("photo.jpg")

ImagePack.compress(jpeg, algo: :jpeg_turbo, quality: 82)
ImagePack.compress(jpeg, algo: :mozjpeg, quality: 82)

# SSIM-guarded compression: choose the smallest acceptable JPEG quality
# while keeping decoded visual similarity >= 0.985.
ImagePack.compress(jpeg, algo: :mozjpeg, min_ssim: 0.985)

# Start at quality 75, but raise quality if needed to satisfy min_ssim.
ImagePack.compress(jpeg, algo: :mozjpeg, quality: 75, min_ssim: 0.985)

ImagePack.compress("photo.jpg", output: "photo.optimized.jpg", algo: :mozjpeg)
ImagePack.compress_file("photo.jpg", output: "photo.optimized.jpg", algo: :mozjpeg)
ImagePack.compress_bytes(jpeg, algo: :fast, quality: 82)

# Lossless coefficient-level JPEG optimization/transcode.
# This rewrites JPEG coefficients without decoding/re-encoding pixels.
ImagePack.optimize_jpeg(jpeg, progressive: true, strip_metadata: false)
ImagePack.optimize_file("photo.jpg", output: "photo.lossless.jpg")
ImagePack.optimize_bytes(jpeg)

ImagePack.compress_pixels(rgb_buffer,
  width: 1920,
  height: 1080,
  channels: 3,
  algo: :mozjpeg
)

ImagePack.inspect_image(jpeg)
# => { format: :jpeg, width: 1920, height: 1080, channels: 3, bit_depth: 8, decoded_bytes: 6220800 }
```

## Input / output policy

`compress(input, ...)` accepts JPEG input only:

- binary `String` (`ASCII-8BIT`) — JPEG bytes;
- non-binary `String` that points to an existing file — file path;
- `Pathname` — file path;
- `IO::Buffer` — JPEG bytes, copied before native no-GVL execution for safety.

For non-ambiguous call sites prefer `compress_bytes(bytes, ...)` or `compress_file(path, ...)`.

`compress_pixels(buffer, ...)` accepts raw pixels as binary `String` or `IO::Buffer`.
`channels` must be `1`, `3`, or `4`. Alpha in RGBA input is dropped in v0.2.2; pass `drop_alpha: true` to make that explicit, or `drop_alpha: false` to reject it.

`output: nil` returns binary JPEG bytes. `output: String/Pathname` writes through a temporary file and renames it into place.
Streaming output is intentionally not supported in v0.2.2.

## Lossless JPEG optimize

`optimize_jpeg`, `optimize_bytes`, and `optimize_file` are coefficient-level JPEG transcode helpers. They use `jpeg_read_coefficients` / `jpeg_write_coefficients`, so pixels are not decoded and re-encoded. This is the preferred path when you only want to rewrite an existing JPEG with optimized Huffman tables and optional progressive scans.

Defaults are intentionally conservative:

- `progressive: true` creates a progressive optimized JPEG;
- `strip_metadata: false` preserves APP/COM metadata by default because this path is meant to be visually/losslessly safe.

If `strip_metadata: true` is requested and the source JPEG has EXIF Orientation, `optimize_jpeg` raises `UnsupportedError` instead of silently removing the orientation tag and changing how viewers display the image. Use `strip_metadata: false` for coefficient-level optimization, or `compress(..., strip_metadata: true)` if you want pixel-level orientation normalization.

## SSIM guard

`compress` and `compress_pixels` accept `min_ssim:`. This enables a native guarded path:

1. decode the original JPEG to reference pixels;
2. encode trial JPEG candidates with the existing MozJPEG/libjpeg backend;
3. decode each candidate;
4. compute a native luma SSIM score;
5. return the smallest quality that satisfies `SSIM >= min_ssim`.

If only `min_ssim:` is provided, the search starts at quality `1`. If both `quality:`
and `min_ssim:` are provided, `quality:` is treated as the lower starting point and
the encoder raises quality only if the candidate violates the SSIM floor.

If no quality can satisfy the requested floor, `ImagePack::QualityConstraintError`
is raised. With `execution: :auto`, guarded compression uses the no-GVL/offload
path instead of the small-image direct path because it may encode/decode several
candidates. For `compress_pixels`, the reference is the raw pixel buffer itself, not a seed JPEG. RGBA + `min_ssim` is rejected because JPEG cannot represent alpha.

The metric is a fast native luma SSIM-like guard based on 8x8 blocks, not a full Wang-style Gaussian-window reference implementation. It is intended as a compression guard, not as a general-purpose image quality benchmark.

## Execution modes

```ruby
ImagePack.compress(jpeg, execution: :direct)
ImagePack.compress(jpeg, execution: :nogvl)
ImagePack.compress(jpeg, execution: :offload)
ImagePack.compress(jpeg, execution: :auto)
```

- `:direct` — executes under GVL, intended for small images.
- `:nogvl` — uses `rb_nogvl(..., flags: 0)`.
- `:offload` — uses `rb_nogvl(..., RB_NOGVL_OFFLOAD_SAFE)` on Ruby 3.4+.
- `:auto` — header-first policy chooses direct/nogvl/offload.

## Cancellation

`cancellable: true` is supported for `execution: :nogvl`, `:offload`, or `:auto`.
Cancellation is cooperative at decode, encode, and SSIM-search checkpoints, not instant.

## Metadata / EXIF orientation

`strip_metadata: true` removes metadata. If the source JPEG contains EXIF Orientation, `image_pack` applies that orientation to decoded pixels before stripping metadata, so the visual orientation is preserved. With `strip_metadata: false`, APP/COM markers are preserved across both fast and size-oriented paths.

## Build info

```ruby
ImagePack.build_info
# => { version: "0.2.2", mozjpeg: "4.1.5", simd: true }
```

## Vendoring

The gem is expected to ship vendored native sources/libs, so end users should not install system libjpeg/mozjpeg.
For repository preparation:

```bash
bundle exec rake vendor
bundle exec rake compile
```

`rake vendor` pins MozJPEG `v4.1.5`.

## Current limitations

- Pixel-level `compress` rejects CMYK/YCCK JPEG input because it decodes to RGB/gray before re-encoding. Use `optimize_jpeg` for coefficient-level lossless optimization of existing CMYK/YCCK JPEGs.
- Arithmetic-coded JPEG support is disabled in the vendored `jconfig.h` for v0.2.2.
- The SSIM guard is a fast 8x8 luma block metric and still assumes quality/SSIM monotonicity during binary search.
- `compress(input, ...)` still has a legacy path-vs-bytes heuristic for non-binary `String`; prefer `compress_bytes` / `compress_file` and `optimize_bytes` / `optimize_file` in new code.

## What is intentionally not included

- Jpegli / C++ code
- AVIF/WebP/PNG
- FFI
- shell-out
- external tempfiles for in-memory output
- byte-size targets / max-bytes policy
