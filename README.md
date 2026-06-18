# image_pack

Ruby-native, in-process JPEG compression backed by vendored MozJPEG.

`0.3.0` is an intentional breaking release: compression now uses explicit `engine:` names. The default engine is `:mozjpeg`, which is compiled into the gem and always works with no external dependencies. `:jpegli` is an opt-in, **experimental** engine that requires an external `cjpegli` helper; it is never selected implicitly.

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

## Engines

```ruby
ImagePack.compress_bytes(jpeg, engine: :mozjpeg, quality: 82) # default
ImagePack.compress_bytes(jpeg, engine: :turbo,   quality: 82)
ImagePack.compress_bytes(jpeg, engine: :jpegli,  quality: 82) # experimental, opt-in
```

Engines:

- `:mozjpeg` — default; in-process, size-oriented MozJPEG path, compiled from vendored sources.
- `:turbo` — in-process, fastest MozJPEG/libjpeg-compatible path.
- `:jpegli` — **experimental, opt-in only**; strongest quality-per-byte path, outputs normal JPEG. It shells out to an external `cjpegli` helper, validates that the helper returned a real JPEG, and is never chosen implicitly. Requesting it without a working helper raises.

`min_ssim:` is supported only for the in-process engines (`:mozjpeg`, `:turbo`), not for `:jpegli`.

Migration from `0.2.x`:

```ruby
# 0.2.x
ImagePack.compress_bytes(jpeg, algo: :fast)
ImagePack.compress_bytes(jpeg, algo: :size)

# 0.3.0
ImagePack.compress_bytes(jpeg, engine: :turbo)
ImagePack.compress_bytes(jpeg, engine: :mozjpeg)
```

`algo:` and `:fast` / `:size` are removed.

## Compression options

```ruby
ImagePack.compress_bytes(jpeg, min_ssim: 0.985)
ImagePack.compress_bytes(jpeg, progressive: true)
ImagePack.compress_bytes(jpeg, strict: true)
ImagePack.compress_bytes(jpeg, report: true)
```

`min_ssim:` searches for the lowest acceptable quality using a native luma SSIM guard.

`strict: true` raises `ImagePack::InvalidImageError` on damaged/truncated JPEG warnings.

`report: true` returns:

```ruby
{
  output: "\xFF\xD8...",
  quality: 84,
  ssim: 0.9861,
  engine: :mozjpeg,
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

This rewrites JPEG coefficients without decoding and re-encoding pixels. Use it when you only want optimized Huffman tables and optional progressive scans.

Defaults: `progressive: true`, `strip_metadata: false`.

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

## Runtime info

```ruby
ImagePack.build_info
# => { version:, mozjpeg:, jpegli:, engines:, default_engine:, simd:, offload_safe: }
```

`engine: :jpegli` (experimental) shells out to an external `cjpegli` executable, resolved at runtime from `IMAGE_PACK_JPEGLI_BIN` or `PATH`, and runs only off the GVL. No jpegli binary is shipped in the gem, and `cjpegli` is never used unless `engine: :jpegli` is requested explicitly. The helper runs with its stdio redirected to `/dev/null`, its output is validated through the native JPEG parser, and it is killed after `IMAGE_PACK_JPEGLI_TIMEOUT_MS` (default 15000; `0` disables). `cancellable: true` is not supported for `:jpegli`; Ruby thread interruption still terminates the child process. `rake vendor` with `IMAGE_PACK_VENDOR_JPEGLI=1` can build `cjpegli` locally for development. Use `ImagePack.jpegli_available?` or `ImagePack.build_info` to check whether the helper was found.

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

`rake vendor` pins MozJPEG `v4.1.5`. The optional jpegli/JPEG XL `v0.11.2` helper is built only when `IMAGE_PACK_VENDOR_JPEGLI=1` is set.

## Limits

- JPEG output only.
- Ruby `>= 3.1`; `execution: :offload` requires Ruby `>= 3.4`.
- `engine: :jpegli` is experimental and opt-in: it requires an external `cjpegli` (from `IMAGE_PACK_JPEGLI_BIN` or `PATH`), runs off the GVL, validates output, and times out. It is never selected by the default; the default `:mozjpeg` is fully in-process.
- `min_ssim:` is not supported for `engine: :jpegli`.
- Pixel-level `compress` rejects CMYK/YCCK JPEG input; use `optimize_jpeg` for existing CMYK/YCCK JPEGs.
- Streaming output is not supported; file output uses atomic write-through-temp-file and rename.
- `ImagePack.compress(input, ...)` keeps a legacy path-vs-bytes heuristic; prefer explicit `*_bytes` / `*_file` helpers.
