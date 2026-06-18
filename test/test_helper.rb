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

  def sample_jpeg(engine: :turbo, quality: 82, width: DEFAULT_WIDTH, height: DEFAULT_HEIGHT)
    ImagePack.compress_pixels(
      sample_pixels(width: width, height: height),
      width: width,
      height: height,
      channels: DEFAULT_CHANNELS,
      engine: engine,
      quality: quality,
      execution: :direct
    )
  end

  def with_env(values)
    previous = {}
    values.each do |key, value|
      previous[key] = ENV.key?(key) ? ENV[key] : :__image_pack_missing_env__
      value.nil? ? ENV.delete(key) : ENV[key] = value.to_s
    end

    yield
  ensure
    previous&.each do |key, value|
      value == :__image_pack_missing_env__ ? ENV.delete(key) : ENV[key] = value
    end
  end

  def with_fake_cjpegli(mode, valid_jpeg: nil)
    Dir.mktmpdir("image_pack_cjpegli") do |dir|
      helper = File.join(dir, "cjpegli")
      valid_path = File.join(dir, "valid.jpg")
      File.binwrite(valid_path, valid_jpeg) if valid_jpeg
      File.write(helper, fake_cjpegli_source)
      File.chmod(0o755, helper)

      env = {
        "IMAGE_PACK_JPEGLI_BIN" => helper,
        "IMAGE_PACK_TEST_CJPEGLI_MODE" => mode,
        "IMAGE_PACK_TEST_CJPEGLI_VALID" => valid_jpeg ? valid_path : nil
      }

      with_env(env) { yield helper }
    end
  end

  def fake_cjpegli_source
    <<~RUBY
      #!/usr/bin/env ruby
      mode = ENV.fetch("IMAGE_PACK_TEST_CJPEGLI_MODE")
      output = ARGV.fetch(-1)

      case mode
      when "copy"
        File.binwrite(output, File.binread(ENV.fetch("IMAGE_PACK_TEST_CJPEGLI_VALID")))
      when "marker_only"
        File.binwrite(output, [0xFF, 0xD8].pack("C*") + "not real jpeg".b + [0xFF, 0xD9].pack("C*"))
      when "exit"
        exit 42
      when "sleep"
        sleep 30
      else
        abort "unknown fake cjpegli mode"
      end
    RUBY
  end

  def assert_jpeg(bytes)
    assert_instance_of String, bytes
    assert_equal Encoding::BINARY, bytes.encoding
    assert_operator bytes.bytesize, :>, 16
    info = ImagePack.inspect_image(bytes)
    assert_equal :jpeg, info[:format]
    assert_operator info[:width], :>, 0
    assert_operator info[:height], :>, 0
  end
end
