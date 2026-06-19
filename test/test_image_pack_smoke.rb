# frozen_string_literal: true

require_relative "test_helper"

class TestImagePackSmoke < Minitest::Test
  include ImagePackTestHelpers

  def test_compress_pixels_returns_valid_jpeg_for_both_algorithms
    %i[jpeg_turbo mozjpeg].each do |algo|
      out = ImagePack.compress_pixels(
        sample_pixels,
        width: DEFAULT_WIDTH,
        height: DEFAULT_HEIGHT,
        channels: DEFAULT_CHANNELS,
        algo: algo,
        quality: 82,
        execution: :direct
      )

      assert_jpeg out
    end
  end

  def test_compress_jpeg_input_returns_valid_jpeg_for_both_algorithms
    input = sample_jpeg(algo: :jpeg_turbo, quality: 90)

    %i[jpeg_turbo mozjpeg].each do |algo|
      out = ImagePack.compress(input, algo: algo, quality: 82, execution: :nogvl)

      assert_jpeg out
    end
  end

  def test_default_compress_uses_mozjpeg_size_profile
    input = sample_jpeg(algo: :jpeg_turbo, quality: 90)

    default_out = ImagePack.compress(input, quality: 82, execution: :direct)
    mozjpeg_out = ImagePack.compress(input, algo: :mozjpeg, quality: 82, execution: :direct)

    assert_equal mozjpeg_out, default_out
    assert_jpeg default_out
  end

  def test_legacy_aliases_still_match_their_native_profiles
    input = sample_jpeg(algo: :jpeg_turbo, quality: 90)

    assert_equal ImagePack.compress(input, algo: :jpeg_turbo, quality: 82, execution: :direct),
                 ImagePack.compress(input, algo: :fast, quality: 82, execution: :direct)
    assert_equal ImagePack.compress(input, algo: :mozjpeg, quality: 82, execution: :direct),
                 ImagePack.compress(input, algo: :size, quality: 82, execution: :direct)
  end

  def test_mozjpeg_default_uses_progressive_profile
    input = sample_jpeg(algo: :jpeg_turbo, quality: 90)

    default_out = ImagePack.compress(input, algo: :mozjpeg, quality: 82, execution: :direct)
    explicit_out = ImagePack.compress(input, algo: :mozjpeg, quality: 82, progressive: true, execution: :direct)

    assert_equal explicit_out, default_out
    assert_jpeg default_out
  end

  def test_jpeg_turbo_default_keeps_baseline_profile
    input = sample_jpeg(algo: :jpeg_turbo, quality: 90)

    default_out = ImagePack.compress(input, algo: :jpeg_turbo, quality: 82, execution: :direct)
    explicit_out = ImagePack.compress(input, algo: :jpeg_turbo, quality: 82, progressive: false, execution: :direct)

    assert_equal explicit_out, default_out
    assert_jpeg default_out
  end



  def test_optimize_jpeg_returns_valid_jpeg_without_pixel_reencode
    input = sample_jpeg(algo: :jpeg_turbo, quality: 90)

    out = ImagePack.optimize_jpeg(input, progressive: true, strip_metadata: false, execution: :direct)

    assert_jpeg out
    info = ImagePack.inspect_image(out)
    assert_equal DEFAULT_WIDTH, info[:width]
    assert_equal DEFAULT_HEIGHT, info[:height]
  end

  def test_inspect_image_reports_basic_metadata
    input = sample_jpeg(algo: :jpeg_turbo, quality: 82)
    info = ImagePack.inspect_image(input)

    assert_equal :jpeg, info[:format]
    assert_equal DEFAULT_WIDTH, info[:width]
    assert_equal DEFAULT_HEIGHT, info[:height]
    assert_equal DEFAULT_CHANNELS, info[:channels]
    assert_equal 8, info[:bit_depth]
    assert_includes %i[ycbcr rgb], info[:color_space]
    assert_operator info[:decoded_bytes], :>, 0
  end
end
