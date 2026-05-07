# frozen_string_literal: true

require_relative "test_helper"

class TestValidation < Minitest::Test
  include ImagePackTestHelpers

  def test_rejects_unknown_algorithm
    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.compress_pixels(sample_pixels,
                                width: DEFAULT_WIDTH,
                                height: DEFAULT_HEIGHT,
                                channels: DEFAULT_CHANNELS,
                                algo: :unknown)
    end
  end

  def test_rejects_invalid_quality
    [0, 101, "82"].each do |quality|
      assert_raises(ImagePack::InvalidArgumentError) do
        ImagePack.compress_pixels(sample_pixels,
                                  width: DEFAULT_WIDTH,
                                  height: DEFAULT_HEIGHT,
                                  channels: DEFAULT_CHANNELS,
                                  quality: quality)
      end
    end
  end

  def test_rejects_invalid_min_ssim
    [0.0, -0.1, 1.1, "0.985"].each do |min_ssim|
      assert_raises(ImagePack::InvalidArgumentError) do
        ImagePack.compress(sample_jpeg,
                           algo: :jpeg_turbo,
                           min_ssim: min_ssim,
                           execution: :direct)
      end
    end
  end

  def test_rejects_invalid_jpeg_bytes
    assert_raises(ImagePack::InvalidImageError) do
      ImagePack.inspect_image("not a jpeg".b)
    end
  end

  def test_respects_max_pixels_limit
    old = ImagePack.configuration.max_pixels
    ImagePack.configuration.max_pixels = 10

    assert_raises(ImagePack::LimitExceededError) do
      ImagePack.compress_pixels(sample_pixels(width: 16, height: 16),
                                width: 16,
                                height: 16,
                                channels: 3,
                                algo: :jpeg_turbo,
                                quality: 82,
                                execution: :direct)
    end
  ensure
    ImagePack.configuration.max_pixels = old
  end
end
