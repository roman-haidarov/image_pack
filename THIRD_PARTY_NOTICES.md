# Third-party notices

This gem compiles one third-party JPEG library from vendored source. It does not
redistribute any prebuilt third-party binary.

## MozJPEG

- Project: https://github.com/mozilla/mozjpeg
- License: BSD-style / IJG / zlib-compatible notices as provided by upstream.
- Usage: statically compiled into the native extension from vendored sources for
  `engine: :turbo`, `engine: :mozjpeg`, JPEG decode, the SSIM guard decode, and
  the lossless optimize path.

## jpegli / JPEG XL (optional, not bundled)

- Project: https://github.com/libjxl/libjxl
- License: BSD-3-Clause as provided by upstream.
- Usage: `engine: :jpegli` shells out to an external `cjpegli` executable resolved
  at runtime from `IMAGE_PACK_JPEGLI_BIN` or `PATH`. No jpegli code or binary is
  shipped in the packaged gem; the user or platform provides `cjpegli`. The
  default engine is `:mozjpeg`; explicit `engine: :jpegli` raises when `cjpegli`
  is unavailable.
