# frozen_string_literal: true

require_relative "test_helper"

class TestJpegQuality < Minitest::Test
  include ImagePackTestHelpers

  def test_jpeg_input_quality_changes_turbo_output_size
    input = sample_jpeg(engine: :turbo, quality: 90, width: 128, height: 96)

    low = ImagePack.compress(input, engine: :turbo, quality: 20, execution: :nogvl)
    high = ImagePack.compress(input, engine: :turbo, quality: 95, execution: :nogvl)

    assert_jpeg low
    assert_jpeg high
    refute_equal low.bytesize, high.bytesize
    assert_operator low.bytesize, :<, high.bytesize
  end

  def test_jpeg_input_quality_changes_mozjpeg_output_size
    input = sample_jpeg(engine: :turbo, quality: 90, width: 128, height: 96)

    low = ImagePack.compress(input, engine: :mozjpeg, quality: 20, execution: :nogvl)
    high = ImagePack.compress(input, engine: :mozjpeg, quality: 95, execution: :nogvl)

    assert_jpeg low
    assert_jpeg high
    refute_equal low.bytesize, high.bytesize
    assert_operator low.bytesize, :<, high.bytesize
  end

  def test_pixel_input_quality_changes_turbo_output_size
    pixels = sample_pixels(width: 128, height: 96)

    low = ImagePack.compress_pixels(pixels, width: 128, height: 96, channels: 3,
                                    engine: :turbo, quality: 20, execution: :direct)
    high = ImagePack.compress_pixels(pixels, width: 128, height: 96, channels: 3,
                                     engine: :turbo, quality: 95, execution: :direct)

    assert_jpeg low
    assert_jpeg high
    assert_operator low.bytesize, :<, high.bytesize
  end

  def test_min_ssim_guard_returns_valid_jpeg
    input = sample_jpeg(engine: :turbo, quality: 90, width: 128, height: 96)

    out = ImagePack.compress(input, engine: :turbo, min_ssim: 0.985, execution: :direct)

    assert_jpeg out
  end

  def test_min_ssim_guard_raises_quality_above_low_start
    input = sample_jpeg(engine: :turbo, quality: 90, width: 128, height: 96)

    low = ImagePack.compress(input, engine: :turbo, quality: 20, execution: :direct)
    guarded = ImagePack.compress(input,
                                  engine: :turbo,
                                  quality: 20,
                                  min_ssim: 0.995,
                                  execution: :direct)

    assert_jpeg guarded
    assert_operator guarded.bytesize, :>, low.bytesize
  end
  def test_pixel_min_ssim_without_quality_starts_from_lowest_quality
    pixels = sample_pixels(width: 128, height: 96)

    default_guard = ImagePack.compress_pixels(pixels,
                                              width: 128,
                                              height: 96,
                                              channels: 3,
                                              engine: :turbo,
                                              min_ssim: 0.10,
                                              execution: :direct)
    explicit_low = ImagePack.compress_pixels(pixels,
                                             width: 128,
                                             height: 96,
                                             channels: 3,
                                             engine: :turbo,
                                             quality: 1,
                                             min_ssim: 0.10,
                                             execution: :direct)

    assert_jpeg default_guard
    assert_jpeg explicit_low
    assert_equal explicit_low.bytesize, default_guard.bytesize
  end

  def test_compress_pixels_accepts_mozjpeg_trellis_option
    pixels = sample_pixels(width: 96, height: 80)

    out = ImagePack.compress_pixels(pixels,
                                    width: 96,
                                    height: 80,
                                    channels: 3,
                                    engine: :mozjpeg,
                                    mozjpeg_trellis: false,
                                    quality: 82,
                                    execution: :direct)

    assert_jpeg out
  end

end
