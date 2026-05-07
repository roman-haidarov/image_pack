# Changelog

## 0.2.0

- Added `min_ssim:` to `ImagePack.compress` for SSIM-guarded JPEG compression.
- Added native luma SSIM scoring and guarded quality search on the existing MozJPEG/libjpeg path.
- Added `ImagePack::QualityConstraintError` when no JPEG quality can satisfy the requested SSIM floor.
- Bumped gem version to `0.2.0`.

## 0.1.0

- Reworked native extension to a single `ext/image_pack/image_pack.c` translation unit for the first prototype.

- Initial pure-C prototype of `image_pack`.
- Public API: `ImagePack.compress`, `ImagePack.compress_pixels`, `ImagePack.inspect_image`.
- Native execution modes: `:direct`, `:nogvl`, `:offload`, `:auto`.
- Algorithm modes: `:jpeg_turbo`, `:mozjpeg`.
- Jpegli removed: no C++ compiler or C++ runtime is required.
