# frozen_string_literal: true

require_relative "test_helper"
require "timeout"

class TestReportAndStrict < Minitest::Test
  include ImagePackTestHelpers

  def big_jpeg
    sample_jpeg(engine: :mozjpeg, quality: 90, width: 400, height: 400)
  end

  def truncated_jpeg(fraction: 0.6)
    jpeg = big_jpeg
    jpeg[0, (jpeg.bytesize * fraction).to_i].b
  end

  def test_report_true_returns_hash_for_bytes_output
    result = ImagePack.compress(big_jpeg, engine: :mozjpeg, quality: 70, report: true)

    assert_kind_of Hash, result
    assert_kind_of String, result[:output]
    assert_equal Encoding::ASCII_8BIT, result[:output].encoding
    assert_equal 70, result[:quality]
    assert_nil result[:ssim]
    assert_equal :mozjpeg, result[:engine]
    assert_equal result[:output].bytesize, result[:bytesize]
    assert_operator result[:input_bytesize], :>, 0
    assert_equal 0, result[:warning_count]
    assert_nil result[:warning]
  end

  def test_report_true_exposes_ssim_selected_quality
    result = ImagePack.compress(big_jpeg, engine: :mozjpeg, min_ssim: 0.97, report: true)

    assert_kind_of Float, result[:ssim]
    assert_operator result[:ssim], :>=, 0.97
    assert_includes 1..100, result[:quality]
  end

  def test_report_true_for_file_output
    Dir.mktmpdir do |dir|
      path = File.join(dir, "out.jpg")
      result = ImagePack.compress(big_jpeg, output: path, engine: :mozjpeg, quality: 75, report: true)

      assert_equal true, result[:output]
      assert_equal 75, result[:quality]
      assert File.file?(path)
      assert_equal File.size(path), result[:bytesize]
    end
  end

  def test_report_false_preserves_legacy_return
    out = ImagePack.compress(big_jpeg, engine: :mozjpeg, quality: 70)

    assert_kind_of String, out
    assert_equal Encoding::ASCII_8BIT, out.encoding
  end

  def test_report_true_for_compress_pixels
    pixels = sample_pixels(width: 200, height: 160)
    result = ImagePack.compress_pixels(pixels, width: 200, height: 160, channels: 3,
                                       engine: :mozjpeg, min_ssim: 0.95, report: true)

    assert_kind_of Float, result[:ssim]
    assert_operator result[:ssim], :>=, 0.95
    assert_equal :mozjpeg, result[:engine]
  end

  def test_report_must_be_boolean
    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.compress(big_jpeg, report: "yes")
    end
  end

  def test_warnings_collected_on_truncated_input
    result = ImagePack.compress(truncated_jpeg, engine: :mozjpeg, report: true)

    assert_operator result[:warning_count], :>, 0
    assert_kind_of String, result[:warning]
  end

  def test_strict_rejects_truncated_input
    assert_raises(ImagePack::InvalidImageError) do
      ImagePack.compress(truncated_jpeg, engine: :mozjpeg, strict: true)
    end
  end

  def test_strict_accepts_clean_input
    out = ImagePack.compress(big_jpeg, engine: :mozjpeg, strict: true)

    assert_kind_of String, out
    assert_operator out.bytesize, :>, 0
  end

  def test_strict_must_be_boolean
    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.compress(big_jpeg, strict: "yes")
    end
  end

  def test_optimize_accepts_strict
    out = ImagePack.optimize_jpeg(big_jpeg, strict: true)

    assert_kind_of String, out
    assert_operator out.bytesize, :>, 0
  end

  def test_cancelled_error_is_part_of_hierarchy
    assert_operator ImagePack::CancelledError, :<, ImagePack::Error
  end

  def test_cancellable_true_is_accepted_for_nogvl
    out = ImagePack.compress(big_jpeg, engine: :mozjpeg, execution: :nogvl, cancellable: true)

    assert_kind_of String, out
  end

  def test_cancellable_true_rejected_for_direct
    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.compress(big_jpeg, execution: :direct, cancellable: true)
    end
  end

  def test_raised_exception_propagates_from_async_path
    pixels = sample_pixels(width: 2600, height: 2600)
    thread = Thread.new do
      Thread.current.report_on_exception = false
      ImagePack.compress_pixels(pixels, width: 2600, height: 2600, channels: 3,
                                engine: :mozjpeg, execution: :nogvl, cancellable: true, min_ssim: 0.99)
      :completed
    rescue ImagePack::CancelledError
      :cancelled
    end

    sleep 0.02 until thread.status == "sleep" || thread.stop? || !thread.alive?
    thread.raise(ImagePack::CancelledError.new("cancel"))

    value = Timeout.timeout(5) { thread.value }

    assert_equal :cancelled, value
  end

  def test_offload_unavailable_message_mentions_runtime_gate
    skip "offload is available in this runtime" if ImagePack.offload_safe?

    error = assert_raises(ImagePack::UnsupportedError) do
      ImagePack.compress(big_jpeg, engine: :mozjpeg, execution: :offload)
    end

    assert_match(/unavailable in this runtime/, error.message)
    assert_match(/IMAGE_PACK_DISABLE_OFFLOAD/, error.message)
  end

  def test_offload_execution_when_runtime_supports_it
    skip "offload is unavailable in this runtime" unless ImagePack.offload_safe?

    out = ImagePack.compress(big_jpeg, engine: :mozjpeg, execution: :offload)

    assert_jpeg(out)
  end

  def test_no_stale_version_in_output_error
    error = assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.compress(big_jpeg, output: 123)
    end

    refute_match(/0\.2\.\d/, error.message)
  end
end
