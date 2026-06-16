# Changelog

## 0.2.2

- Fixed native cleanup safety: native contexts are now released through `rb_ensure`, including Ruby exception paths from argument coercion, config reads, native status errors, output `String` allocation, and file-output failures.
- Fixed libjpeg `longjmp` cleanup for encode/decode/luma-decode transient buffers.
- Fixed `max_output_size = 0` semantics; all size/dimension limits now consistently treat `0` as disabled.
- Fixed `compress_pixels(min_ssim:)`: it now uses the raw pixel buffer as the reference instead of a temporary quality-95 seed JPEG, and it respects the requested execution mode.
- Fixed metadata preservation drift: `strip_metadata: false` now preserves markers across fast, size, and SSIM paths.
- Fixed EXIF Orientation stripping: when metadata is stripped, orientation is applied to decoded pixels before output.
- Fixed `progressive:` for `algo: :mozjpeg`; `progressive: false` no longer silently inherits the max-compression progressive scan script.
- Added decode/luma-decode cancellation checkpoints.
- Reduced avoidable copying for explicit `execution: :direct` String/pixel inputs.
- Batched RGBA→RGB scanline encoding instead of writing one line at a time.
- Decoded SSIM candidate luma directly as grayscale instead of RGB + manual luma conversion.
- Made output-path writes safer by writing to a temporary file, checking `fclose`, and renaming into place.
- Added `compress_bytes`, `compress_file`, `optimize_jpeg`, `optimize_bytes`, `optimize_file`, and `build_info`.
- Added coefficient-level lossless JPEG optimization through `jpeg_read_coefficients` / `jpeg_write_coefficients`; this avoids decode→pixels→re-encode when only optimizing an existing JPEG.
- Made private native entrypoints private class methods.
- Added `-fvisibility=hidden` and `IMAGE_PACK_REQUIRE_SIMD=1` build guard.

## 0.2.1

- `ip_compute_ssim_luma_buffer`: rewrote inner accumulators from `double` to
  `int32_t`. For an 8x8 luma window all partial sums (sum, sum-of-squares,
  cross-product) fit in 32 bits. GCC was already auto-vectorizing the
  `double` version on AVX2 (4 lanes × fp64), but int32 doubles the lane
  count (8 lanes × i32) and uses cheaper integer multiplies. Split the
  kernel into a fixed-size 8x8 specialization plus a variable edge kernel.
  Top hot symbol in the SSIM-guarded perf profile.
- `ip_build_luma_buffer`: split the runtime-strided BT.601 loop into
  channels==3 and channels==4 specializations with `__restrict__` pointers
  so the compiler can vectorize per-pixel work.
- `prepare_encode_row` (RGBA→RGB): `__restrict__` + hoisted width to enable
  vectorization by the compiler.
- `ip_malloc_hot`: new helper for buffers we touch in tight loops right
  after allocation. On Linux it issues `madvise(MADV_HUGEPAGE)` for
  allocations >= 256 KiB to remove the per-cacheline minor page faults
  that previously appeared inside `jsimd_*_avx2` hot loops in the perf
  profile. No-op on macOS / non-Linux. Used for the decoded pixel buffer,
  the SSIM luma buffer, and the pixel input buffer.
- SSIM-guarded path: reference and candidate decodes both now use
  `fast_decode_mode=1` (no fancy upsampling, no block smoothing). The
  comparison stays apples-to-apples since both sides use the same decode
  pipeline; ~30% reduction in candidate-decode cost.
- `extconf.rb`: opt-in `IMAGE_PACK_MARCH=<arch>` env knob for tuned
  builds (`native`, `x86-64-v3`, etc). Default build stays portable.
  Also added `-fno-math-errno -fno-trapping-math` to remove libm-related
  vectorization barriers without changing semantics for the integer hot
  paths.
- CI: Linux x86_64 jobs now compile with `IMAGE_PACK_MARCH=x86-64-v3`
  (AVX2 baseline; covers all current GitHub-hosted runner generations).
  arm64 / macOS unchanged.

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
