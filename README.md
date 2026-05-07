# image_pack

`image_pack` is a Ruby-native JPEG compressor prototype for Ruby `>= 3.4.0`.

Current version: `0.2.0`.

This pure-C variant intentionally removes Jpegli and any C++ toolchain requirement.
The native layer is written in C and links against vendored MozJPEG, which is libjpeg-compatible and based on libjpeg-turbo.

## Backends / modes

```ruby
ImagePack.compress(input, algo: :jpeg_turbo)
ImagePack.compress(input, algo: :mozjpeg)
```

- `:jpeg_turbo` — fast libjpeg-compatible mode with MozJPEG-specific size optimizations disabled.
- `:mozjpeg` — size-oriented mode using progressive/optimized Huffman coding through the MozJPEG/libjpeg API.

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
- non-binary `String` — file path;
- `Pathname` — file path;
- `IO::Buffer` — JPEG bytes, copied before native no-GVL execution for safety.

`compress_pixels(buffer, ...)` accepts raw pixels as binary `String` or `IO::Buffer`.
`channels` must be `1`, `3`, or `4`. Alpha in RGBA input is dropped in v0.2.0.

`output: nil` returns binary JPEG bytes. `output: String/Pathname` writes to path and returns `true`.
Streaming output is intentionally not supported in v0.2.0.

## SSIM guard

`compress` accepts `min_ssim:` for JPEG input. This enables a native guarded path:

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
candidates. `min_ssim` is intentionally not supported by `compress_pixels` in
this release.

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

`cancellable: true` is supported only for `algo: :mozjpeg` and `execution: :nogvl`, `:offload`, or `:auto`.
Cancellation is cooperative at encoder scanline checkpoints, not instant.

## Vendoring

The gem is expected to ship vendored native sources/libs, so end users should not install system libjpeg/mozjpeg.
For repository preparation:

```bash
bundle exec rake vendor
bundle exec rake compile
```

`rake vendor` pins MozJPEG `v4.1.5`.

## What is intentionally not included

- Jpegli / C++ code
- AVIF/WebP/PNG
- FFI
- shell-out
- tempfiles by default
- byte-size targets / max-bytes policy
