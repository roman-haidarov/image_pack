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

  def test_inspect_image_reports_basic_metadata
    input = sample_jpeg(algo: :jpeg_turbo, quality: 82)
    info = ImagePack.inspect_image(input)

    assert_equal :jpeg, info[:format]
    assert_equal DEFAULT_WIDTH, info[:width]
    assert_equal DEFAULT_HEIGHT, info[:height]
    assert_equal DEFAULT_CHANNELS, info[:channels]
    assert_equal 8, info[:bit_depth]
    assert_operator info[:decoded_bytes], :>, 0
  end
end
