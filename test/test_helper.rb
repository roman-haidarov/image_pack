# frozen_string_literal: true

require "minitest/autorun"
require "tmpdir"
require "image_pack"

module ImagePackTestHelpers
  DEFAULT_WIDTH = 96
  DEFAULT_HEIGHT = 80
  DEFAULT_CHANNELS = 3

  def sample_pixels(width: DEFAULT_WIDTH, height: DEFAULT_HEIGHT, channels: DEFAULT_CHANNELS)
    raise ArgumentError, "channels must be 3 or 4" unless [3, 4].include?(channels)

    bytes = String.new(capacity: width * height * channels, encoding: Encoding::BINARY)

    height.times do |y|
      width.times do |x|
        bytes << ((x * 255) / [width - 1, 1].max)
        bytes << ((y * 255) / [height - 1, 1].max)
        bytes << ((x * 17 + y * 31 + (x * y)) & 0xff)
        bytes << 255 if channels == 4
      end
    end

    bytes
  end

  def sample_jpeg(algo: :jpeg_turbo, quality: 82, width: DEFAULT_WIDTH, height: DEFAULT_HEIGHT)
    ImagePack.compress_pixels(
      sample_pixels(width: width, height: height),
      width: width,
      height: height,
      channels: DEFAULT_CHANNELS,
      algo: algo,
      quality: quality,
      execution: :direct
    )
  end

  def assert_jpeg(bytes)
    assert_instance_of String, bytes
    assert_equal Encoding::BINARY, bytes.encoding
    assert_operator bytes.bytesize, :>, 16
    assert_equal 0xff, bytes.getbyte(0)
    assert_equal 0xd8, bytes.getbyte(1)
    assert_equal 0xff, bytes.getbyte(bytes.bytesize - 2)
    assert_equal 0xd9, bytes.getbyte(bytes.bytesize - 1)
  end
end
