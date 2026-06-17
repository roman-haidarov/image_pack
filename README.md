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

- `:size` / `:mozjpeg` — smaller files, default
- `:fast` / `:jpeg_turbo` — faster mode

Common options:

```ruby
ImagePack.compress_bytes(jpeg, min_ssim: 0.985)
ImagePack.compress_bytes(jpeg, progressive: true)
ImagePack.compress_bytes(jpeg, strict: true)
ImagePack.compress_bytes(jpeg, report: true)
```

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

```bash
bundle exec rake vendor
bundle exec rake compile
bundle exec rake test
```

`rake vendor` pins MozJPEG `v4.1.5`.

## Limits

- JPEG only.
- Ruby `>= 3.1`; `execution: :offload` requires Ruby `>= 3.4`.
- Pixel-level `compress` rejects CMYK/YCCK JPEG input; use `optimize_jpeg` for existing CMYK/YCCK JPEGs.
- Arithmetic-coded JPEG support is disabled in `0.2.4`.
- Streaming output is not supported; file output uses atomic write-through-temp-file and rename.
- `ImagePack.compress(input, ...)` keeps a legacy path-vs-bytes heuristic; prefer explicit `*_bytes` / `*_file` helpers.
