# frozen_string_literal: true

require_relative "test_helper"

class TestIoAndOutput < Minitest::Test
  include ImagePackTestHelpers

  def test_compress_accepts_path_input
    Dir.mktmpdir("image_pack_test") do |dir|
      input_path = File.join(dir, "input.jpg")
      File.binwrite(input_path, sample_jpeg(engine: :turbo, quality: 90))

      out = ImagePack.compress(input_path, engine: :turbo, quality: 82, execution: :direct)

      assert_jpeg out
    end
  end

  def test_compress_pixels_writes_output_path
    Dir.mktmpdir("image_pack_test") do |dir|
      output_path = File.join(dir, "output.jpg")

      result = ImagePack.compress_pixels(
        sample_pixels,
        width: DEFAULT_WIDTH,
        height: DEFAULT_HEIGHT,
        channels: DEFAULT_CHANNELS,
        output: output_path,
        engine: :turbo,
        quality: 82,
        execution: :direct
      )

      assert_equal true, result
      assert File.file?(output_path)
      assert_jpeg File.binread(output_path)
    end
  end

  def test_output_path_overwrites_existing_file
    Dir.mktmpdir("image_pack_test") do |dir|
      output_path = File.join(dir, "output.jpg")
      File.binwrite(output_path, "old".b)

      result = ImagePack.compress_pixels(sample_pixels,
                                         width: DEFAULT_WIDTH,
                                         height: DEFAULT_HEIGHT,
                                         channels: DEFAULT_CHANNELS,
                                         output: output_path,
                                         engine: :turbo,
                                         quality: 82,
                                         execution: :direct)

      assert_equal true, result
      refute_equal "old".b, File.binread(output_path)
      assert_jpeg File.binread(output_path)
    end
  end

  def test_optimize_file_writes_output_path
    Dir.mktmpdir("image_pack_test") do |dir|
      input_path = File.join(dir, "input.jpg")
      output_path = File.join(dir, "output.jpg")
      File.binwrite(input_path, sample_jpeg(engine: :turbo, quality: 90))

      result = ImagePack.optimize_file(input_path, output: output_path, execution: :direct)

      assert_equal true, result
      assert File.file?(output_path)
      assert_jpeg File.binread(output_path)
    end
  end

  def test_compress_pixels_accepts_io_buffer_when_available
    skip "IO::Buffer is unavailable" unless defined?(IO::Buffer)

    buffer = IO::Buffer.for(sample_pixels)
    out = ImagePack.compress_pixels(buffer,
                                    width: DEFAULT_WIDTH,
                                    height: DEFAULT_HEIGHT,
                                    channels: DEFAULT_CHANNELS,
                                    engine: :turbo,
                                    quality: 82,
                                    execution: :direct)

    assert_jpeg out
  end

  def test_compress_pixels_allows_extra_bytes_by_default_but_exact_size_can_reject
    pixels = sample_pixels + "trailing scratch space".b

    out = ImagePack.compress_pixels(pixels,
                                    width: DEFAULT_WIDTH,
                                    height: DEFAULT_HEIGHT,
                                    channels: DEFAULT_CHANNELS,
                                    engine: :turbo,
                                    quality: 82,
                                    execution: :direct)
    assert_jpeg out

    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.compress_pixels(pixels,
                                width: DEFAULT_WIDTH,
                                height: DEFAULT_HEIGHT,
                                channels: DEFAULT_CHANNELS,
                                exact_size: true,
                                engine: :turbo,
                                quality: 82,
                                execution: :direct)
    end
  end
end
