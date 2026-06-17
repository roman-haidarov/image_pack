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
    [0, 101, "82", 82.0].each do |quality|
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

  def test_rejects_non_boolean_options
    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.compress(sample_jpeg, progressive: "false")
    end

    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.optimize_jpeg(sample_jpeg, strip_metadata: 0)
    end

    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.compress_pixels(sample_pixels,
                                width: DEFAULT_WIDTH,
                                height: DEFAULT_HEIGHT,
                                channels: DEFAULT_CHANNELS,
                                exact_size: "true")
    end
  end

  def test_build_info_reports_offload_capability
    assert_includes [true, false], ImagePack.offload_safe?
    assert_equal ImagePack.offload_safe?, ImagePack.build_info[:offload_safe]
  end

  def test_rejects_explicit_offload_when_native_offload_safe_is_unavailable
    skip "native offload is available on this Ruby" if ImagePack.offload_safe?

    assert_raises(ImagePack::UnsupportedError) do
      ImagePack.compress_pixels(sample_pixels,
                                width: DEFAULT_WIDTH,
                                height: DEFAULT_HEIGHT,
                                channels: DEFAULT_CHANNELS,
                                execution: :offload)
    end

    assert_raises(ImagePack::UnsupportedError) do
      ImagePack::Configuration.new.execution = :offload
    end
  end

  def test_rejects_invalid_pixel_buffer_type
    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.compress_pixels(Object.new,
                                width: DEFAULT_WIDTH,
                                height: DEFAULT_HEIGHT,
                                channels: DEFAULT_CHANNELS)
    end
  end

  def test_rejects_invalid_jpeg_bytes
    assert_raises(ImagePack::InvalidImageError) do
      ImagePack.inspect_image("not a jpeg".b)
    end
  end

  def test_configuration_rejects_negative_float_and_string_values
    config = ImagePack::Configuration.new

    [:max_input_size, :max_output_size, :max_pixels,
     :direct_input_threshold, :direct_pixel_threshold,
     :max_width, :max_height].each do |name|
      [-1, 1.5, "1024"].each do |value|
        assert_raises(ImagePack::InvalidArgumentError) do
          config.public_send("#{name}=", value)
        end
      end
    end
  end

  def test_configuration_rejects_invalid_execution
    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack::Configuration.new.execution = "auto"
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

  def test_inspect_image_respects_max_input_size
    old = ImagePack.configuration.max_input_size
    ImagePack.configuration.max_input_size = 1

    assert_raises(ImagePack::LimitExceededError) do
      ImagePack.inspect_image(sample_jpeg)
    end
  ensure
    ImagePack.configuration.max_input_size = old
  end
end
