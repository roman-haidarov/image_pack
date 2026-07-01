# image_pack

Ruby-native JPEG compression and optimization backed by vendored pure-C MozJPEG/libjpeg.

No system `libjpeg`, `mozjpeg`, `git`, or `CMake` is required for gem users.

```ruby
gem "image_pack"
```

```ruby
require "image_pack"
```

## Quick use

```ruby
jpeg = File.binread("photo.jpg")

small = ImagePack.compress_bytes(jpeg, quality: 82)
File.binwrite("photo.small.jpg", small)

ImagePack.compress_file("photo.jpg", output: "photo.small.jpg")
ImagePack.optimize_file("photo.jpg", output: "photo.optimized.jpg")
```

Prefer explicit helpers:

```ruby
ImagePack.compress_bytes(jpeg)
ImagePack.compress_file("photo.jpg", output: "out.jpg")
ImagePack.optimize_bytes(jpeg)
ImagePack.optimize_file("photo.jpg", output: "out.jpg")
```

## Compression

```ruby
ImagePack.compress_bytes(jpeg,
  algo: :size,
  quality: 82,
  strip_metadata: true
)
```

Algorithms:

- `:size` / `:mozjpeg` — smaller files, default; uses optimized progressive MozJPEG output by default
- `:fast` / `:jpeg_turbo` — faster baseline mode

Common options:

```ruby
ImagePack.compress_bytes(jpeg, min_ssim: 0.985)
ImagePack.compress_bytes(jpeg, progressive: false) # force baseline output
ImagePack.compress_bytes(jpeg, strict: true)
ImagePack.compress_bytes(jpeg, report: true)
```


`algo: :size` / `:mozjpeg` now defaults to optimized progressive scans plus scan-aware MozJPEG trellis tuning because that is the strongest built-in size profile used by the gem. Pass `progressive: false` when you explicitly need baseline JPEG output.

`algo: :fast` / `:jpeg_turbo` keeps baseline output by default and remains the throughput path.

`min_ssim:` searches for the lowest acceptable quality using a fast native luma SSIM guard.

`strict: true` raises `ImagePack::InvalidImageError` on damaged/truncated JPEG warnings.

`report: true` returns a Hash:

```ruby
{
  output: "\xFF\xD8...",
  quality: 84,
  ssim: 0.9861,
  algo: :mozjpeg,
  bytesize: 50122,
  input_bytesize: 81344,
  warning_count: 0,
  warning: nil
}
```

With `output: "file.jpg"`, `output` is `true`.

## Lossless optimize

```ruby
ImagePack.optimize_bytes(jpeg)
ImagePack.optimize_file("photo.jpg", output: "photo.optimized.jpg")
```

This rewrites JPEG coefficients without decoding and re-encoding pixels. It is the right path for existing JPEGs when you only want optimized Huffman tables and optional progressive scans.

Defaults: `progressive: true`, `strip_metadata: false`.

If `strip_metadata: true` would remove EXIF Orientation, `optimize_jpeg` raises `UnsupportedError` instead of silently changing visual orientation.

## Raw pixels

```ruby
ImagePack.compress_pixels(rgb,
  width: 1920,
  height: 1080,
  channels: 3,
  output: "frame.jpg"
)
```

`channels` must be `1`, `3`, or `4`. JPEG cannot store alpha, so RGBA input needs explicit opt-in:

```ruby
ImagePack.compress_pixels(rgba, width: 100, height: 100, channels: 4, drop_alpha: true)
```

## Inspect

```ruby
ImagePack.inspect_image(jpeg)
# => { format: :jpeg, width: 1920, height: 1080, channels: 3, bit_depth: 8, decoded_bytes: 6220800 }
```

## Execution

Default mode is `:auto`.

```ruby
ImagePack.compress_bytes(jpeg, execution: :auto)
ImagePack.compress_bytes(jpeg, execution: :direct)
ImagePack.compress_bytes(jpeg, execution: :nogvl)
ImagePack.compress_bytes(jpeg, execution: :offload) # Ruby >= 3.4 only
```

Use `ImagePack.offload_safe?` or `ImagePack.build_info` to inspect runtime support.

Set `IMAGE_PACK_DISABLE_OFFLOAD=1` before loading the gem to disable offload.

Long no-GVL/offload calls can be interrupted by raising into the worker thread:

```ruby
worker = Thread.new { ImagePack.compress_bytes(jpeg, execution: :nogvl, cancellable: true) }
worker.raise(ImagePack::CancelledError, "cancelled")
worker.join
```

## Configuration

```ruby
ImagePack.configure do |config|
  config.execution = :auto
  config.max_input_size = 256 * 1024 * 1024
  config.max_output_size = 256 * 1024 * 1024
  config.max_pixels = 100_000_000
end
```

## Development

Core build and test dependencies work on Ruby 2.7.1+:

```bash
bundle install
bundle exec rake compile
bundle exec rake test
```

Optional comparison benchmarks and Async tooling are kept out of Ruby 2.7 dependency resolution. Enable them on modern Ruby when needed:

```bash
BUNDLE_WITH=modern_development bundle install
```

Vendoring and release checks:

```bash
bundle exec rake vendor
bundle exec rake release:check
```

`rake vendor` pins MozJPEG `v4.1.5`.

`rake release:check` compiles, verifies tests, and fails release builds when SIMD is unavailable. Set `IMAGE_PACK_ALLOW_SCALAR=1` only when intentionally shipping a scalar build.

## Limits

- JPEG only.
- Ruby `>= 2.7.1`; `execution: :offload` requires Ruby `>= 3.4`. On Ruby 2.7–3.3, `:auto` uses `:direct` or `:nogvl`; it never attempts scheduler offload.
- Pixel-level `compress` rejects CMYK/YCCK JPEG input; use `optimize_jpeg` for existing CMYK/YCCK JPEGs.
- Arithmetic-coded JPEG support is disabled in `0.2.5`.
- Streaming output is not supported; file output uses atomic write-through-temp-file and rename.
- `ImagePack.compress(input, ...)` keeps a legacy path-vs-bytes heuristic; prefer explicit `*_bytes` / `*_file` helpers.
