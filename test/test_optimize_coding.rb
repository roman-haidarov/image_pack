# frozen_string_literal: true

require_relative "test_helper"

class TestOptimizedProfiles < Minitest::Test
  include ImagePackTestHelpers

  WIDTH = 96
  HEIGHT = 96

  def graphics_rgb
    @graphics_rgb ||= begin
      buf = +"".b
      HEIGHT.times do |y|
        WIDTH.times do |x|
          if ((x / 8) + (y / 8)).even?
            buf << 240 << 240 << 240
          else
            buf << 20 << 60 << 200
          end
        end
      end
      buf
    end
  end

  def encode(algo, **options)
    ImagePack.compress_pixels(graphics_rgb,
                              width: WIDTH,
                              height: HEIGHT,
                              channels: 3,
                              algo: algo,
                              quality: 82,
                              execution: :direct,
                              **options)
  end

  def progressive_jpeg?(bytes)
    i = 2
    while i + 3 < bytes.bytesize
      i += 1 while i < bytes.bytesize && bytes.getbyte(i) != 0xFF
      i += 1 while i < bytes.bytesize && bytes.getbyte(i) == 0xFF
      marker = bytes.getbyte(i)
      return true if marker == 0xC2
      return false if marker == 0xC0
      return false if marker.nil? || marker == 0xD9 || marker == 0xDA
      return false if i + 2 >= bytes.bytesize
      length = (bytes.getbyte(i + 1) << 8) + bytes.getbyte(i + 2)
      return false if length < 2
      i += 1 + length
    end
    false
  end

  def test_size_default_uses_progressive_profile
    assert progressive_jpeg?(encode(:size))
    assert progressive_jpeg?(encode(:mozjpeg))
  end

  def test_size_allows_baseline_escape_hatch
    refute progressive_jpeg?(encode(:size, progressive: false))
    refute progressive_jpeg?(encode(:mozjpeg, progressive: false))
  end

  def test_fast_profile_stays_baseline_by_default
    refute progressive_jpeg?(encode(:fast))
    refute progressive_jpeg?(encode(:jpeg_turbo))
  end

  def test_legacy_aliases_remain_exact
    assert_equal encode(:jpeg_turbo), encode(:fast)
    assert_equal encode(:mozjpeg), encode(:size)
  end

  def test_size_profile_outputs_valid_jpegs
    [encode(:size), encode(:size, progressive: false), encode(:mozjpeg)].each do |jpeg|
      assert_jpeg jpeg
    end
  end

  def test_optimize_coding_is_not_public_api
    assert_raises(ArgumentError) do
      ImagePack.compress_pixels(graphics_rgb,
                                width: WIDTH,
                                height: HEIGHT,
                                channels: 3,
                                algo: :fast,
                                quality: 82,
                                optimize_coding: true)
    end
  end
end
