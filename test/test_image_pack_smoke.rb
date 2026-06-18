# frozen_string_literal: true

require_relative "test_helper"

class TestImagePackSmoke < Minitest::Test
  include ImagePackTestHelpers

  def test_compress_pixels_returns_valid_jpeg_for_available_engines
    engines = %i[turbo mozjpeg]
    engines << :jpegli if ImagePack.jpegli_available?

    engines.each do |engine|
      out = ImagePack.compress_pixels(
        sample_pixels,
        width: DEFAULT_WIDTH,
        height: DEFAULT_HEIGHT,
        channels: DEFAULT_CHANNELS,
        engine: engine,
        quality: 82,
        execution: :direct
      )

      assert_jpeg out
    end
  end

  def test_compress_jpeg_input_returns_valid_jpeg_for_available_engines
    input = sample_jpeg(engine: :turbo, quality: 90)
    engines = %i[turbo mozjpeg]
    engines << :jpegli if ImagePack.jpegli_available?

    engines.each do |engine|
      out = ImagePack.compress(input, engine: engine, quality: 82, execution: :nogvl)

      assert_jpeg out
    end
  end

  def test_default_engine_call_always_produces_valid_jpeg
    input = sample_jpeg(engine: :turbo, quality: 90)

    out = ImagePack.compress(input, quality: 82)
    assert_jpeg out

    pixels_out = ImagePack.compress_pixels(
      sample_pixels,
      width: DEFAULT_WIDTH,
      height: DEFAULT_HEIGHT,
      channels: DEFAULT_CHANNELS,
      quality: 82
    )
    assert_jpeg pixels_out
  end

  def test_default_engine_is_in_process_and_reported_as_mozjpeg
    input = sample_jpeg(engine: :turbo, quality: 90)
    report = ImagePack.compress(input, quality: 82, report: true)

    assert_jpeg report[:output]
    assert_equal :mozjpeg, report[:engine]
  end

  def test_default_does_not_use_jpegli_even_when_helper_present
    input = sample_jpeg(engine: :turbo, quality: 90)
    valid = sample_jpeg(engine: :turbo, quality: 80)

    with_fake_cjpegli("copy", valid_jpeg: valid) do
      report = ImagePack.compress(input, quality: 82, report: true)

      assert_equal :mozjpeg, report[:engine]
      assert_jpeg report[:output]
    end
  end

  def test_default_min_ssim_is_independent_of_helper_presence
    input = sample_jpeg(engine: :turbo, quality: 90)

    out = ImagePack.compress(input, min_ssim: 0.9)
    assert_jpeg out
  end

  def test_default_engine_constant_is_mozjpeg_and_jpegli_is_experimental
    assert_equal :mozjpeg, ImagePack::DEFAULT_ENGINE
    assert_includes ImagePack::EXPERIMENTAL_ENGINES, :jpegli
    assert_includes ImagePack::ENGINES, :jpegli
  end

  def test_default_compress_does_not_mutate_process_env
    before = ENV["IMAGE_PACK_JPEGLI_BIN"]
    input = sample_jpeg(engine: :turbo, quality: 90)

    ImagePack.compress(input, quality: 82)

    assert_equal before.inspect, ENV["IMAGE_PACK_JPEGLI_BIN"].inspect
  end

  def test_explicit_jpegli_reports_missing_binary_when_unavailable
    skip "jpegli binary is available" if ImagePack.jpegli_available?

    error = assert_raises(ImagePack::UnsupportedError) do
      ImagePack.compress_pixels(
        sample_pixels,
        width: DEFAULT_WIDTH,
        height: DEFAULT_HEIGHT,
        channels: DEFAULT_CHANNELS,
        engine: :jpegli,
        quality: 82
      )
    end

    assert_match(/cjpegli/, error.message)
  end

  def test_min_ssim_is_rejected_for_jpegli
    error = assert_raises(ImagePack::UnsupportedError) do
      ImagePack.compress(sample_jpeg(engine: :turbo, quality: 90), engine: :jpegli, min_ssim: 0.98)
    end

    assert_match(/min_ssim/, error.message)
  end

  def test_jpegli_valid_helper_is_stable
    input = sample_jpeg(engine: :turbo, quality: 90)
    valid = sample_jpeg(engine: :turbo, quality: 80)

    with_fake_cjpegli("copy", valid_jpeg: valid) do
      20.times do
        out = ImagePack.compress(input, engine: :jpegli, quality: 82, execution: :nogvl)
        assert_jpeg out
      end
    end
  end

  def test_jpegli_rejects_marker_only_output
    input = sample_jpeg(engine: :turbo, quality: 90)

    with_fake_cjpegli("marker_only") do
      error = assert_raises(ImagePack::EncodeError) do
        ImagePack.compress(input, engine: :jpegli, quality: 82, execution: :nogvl)
      end

      assert_match(/JPEG|Bogus|empty|missing|invalid/i, error.message)
    end
  end

  def test_jpegli_reports_nonzero_helper_exit
    input = sample_jpeg(engine: :turbo, quality: 90)

    with_fake_cjpegli("exit") do
      error = assert_raises(ImagePack::EncodeError) do
        ImagePack.compress(input, engine: :jpegli, quality: 82, execution: :nogvl)
      end

      assert_match(/cjpegli failed/, error.message)
    end
  end

  def test_jpegli_timeout_kills_helper
    input = sample_jpeg(engine: :turbo, quality: 90)

    with_fake_cjpegli("sleep") do
      with_env("IMAGE_PACK_JPEGLI_TIMEOUT_MS" => "100") do
        error = assert_raises(ImagePack::EncodeError) do
          ImagePack.compress(input, engine: :jpegli, quality: 82, execution: :nogvl)
        end

        assert_match(/timed out/, error.message)
      end
    end
  end

  def test_jpegli_thread_kill_interrupts_helper_before_timeout
    input = sample_jpeg(engine: :turbo, quality: 90)

    with_fake_cjpegli("sleep") do
      with_env("IMAGE_PACK_JPEGLI_TIMEOUT_MS" => "10000") do
        thread = Thread.new do
          ImagePack.compress(input, engine: :jpegli, quality: 82, execution: :nogvl)
        end

        sleep 0.2
        thread.kill

        assert thread.join(2), "jpegli helper was not interrupted"
      end
    end
  end

  def test_cancellable_true_is_rejected_for_jpegli
    input = sample_jpeg(engine: :turbo, quality: 90)
    valid = sample_jpeg(engine: :turbo, quality: 80)

    with_fake_cjpegli("copy", valid_jpeg: valid) do
      error = assert_raises(ImagePack::UnsupportedError) do
        ImagePack.compress(input, engine: :jpegli, quality: 82, execution: :nogvl, cancellable: true)
      end

      assert_match(/cancellable/, error.message)
    end
  end

  def test_optimize_jpeg_returns_valid_jpeg_without_pixel_reencode
    input = sample_jpeg(engine: :turbo, quality: 90)

    out = ImagePack.optimize_jpeg(input, progressive: true, strip_metadata: false, execution: :direct)

    assert_jpeg out
    info = ImagePack.inspect_image(out)
    assert_equal DEFAULT_WIDTH, info[:width]
    assert_equal DEFAULT_HEIGHT, info[:height]
  end

  def test_inspect_image_reports_basic_metadata
    input = sample_jpeg(engine: :turbo, quality: 82)
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
