# frozen_string_literal: true

require "digest"
require_relative "test_helper"

class TestV027Features < Minitest::Test
  include ImagePackTestHelpers

  def test_optimize_baseline_is_stable_and_not_progressive
    input = sample_jpeg(algo: :jpeg_turbo, quality: 90)
    outs = Array.new(8) do
      ImagePack.optimize_bytes(input, progressive: false, execution: :direct)
    end

    hashes = outs.map { |o| Digest::SHA256.hexdigest(o) }.uniq
    assert_equal 1, hashes.size
    refute progressive_jpeg?(outs.first)
    assert_jpeg outs.first
  end

  def test_optimize_progressive_stays_progressive
    input = sample_jpeg(algo: :jpeg_turbo, quality: 90)
    out = ImagePack.optimize_bytes(input, progressive: true, execution: :direct)
    assert progressive_jpeg?(out)
    assert_jpeg out
  end

  def test_compress_keeps_icc_when_stripping_metadata
    with_icc = jpeg_with_icc(sample_jpeg)
    assert with_icc.include?("ICC_PROFILE")

    kept = ImagePack.compress_bytes(with_icc, quality: 82, strip_metadata: true, execution: :direct)
    assert kept.include?("ICC_PROFILE"), "default compress must preserve ICC"

    stripped = ImagePack.compress_bytes(with_icc, quality: 82, strip_metadata: true,
                                                  strip_icc: true, execution: :direct)
    refute stripped.include?("ICC_PROFILE")
  end

  def test_optimize_keeps_icc_when_stripping_other_metadata
    with_icc = jpeg_with_icc(sample_jpeg)
    out = ImagePack.optimize_bytes(with_icc, strip_metadata: true, execution: :direct)
    assert out.include?("ICC_PROFILE")
  end

  def test_marker_bomb_is_rejected_before_optimize
    bomb = marker_bomb(sample_jpeg, count: 80)
    error = assert_raises(ImagePack::LimitExceededError) do
      ImagePack.optimize_bytes(bomb, execution: :direct)
    end
    assert_match(/too many|too large/i, error.message)
  end

  def test_stripped_compress_ignores_unsaved_com_markers
    bomb = marker_bomb(sample_jpeg, count: 80)
    out = ImagePack.compress_bytes(bomb, quality: 82, strip_metadata: true, algo: :jpeg_turbo,
                                   execution: :direct)
    assert_jpeg out
  end

  def test_optimize_without_trim_rejects_unaligned_orientation_strip
    input = jpeg_with_exif_orientation(sample_jpeg(width: 1001, height: 777), 6)
    error = assert_raises(ImagePack::UnsupportedError) do
      ImagePack.optimize_bytes(input, strip_metadata: true, progressive: false, execution: :direct)
    end
    assert_match(/trim/i, error.message)
  end

  def test_optimize_trim_opts_in_to_partial_mcu_crop
    input = jpeg_with_exif_orientation(sample_jpeg(width: 1001, height: 777), 6)
    out = ImagePack.optimize_bytes(input, strip_metadata: true, progressive: false, trim: true,
                                   execution: :direct)
    assert_jpeg out
    info = ImagePack.inspect_image(out)
    assert_operator info[:width], :<, 777
    assert_equal 1001, info[:height]
  end

  def test_subsampling_444_is_written_to_sof
    jpeg = ImagePack.compress_pixels(sample_pixels,
                                     width: DEFAULT_WIDTH,
                                     height: DEFAULT_HEIGHT,
                                     channels: 3,
                                     quality: 82,
                                     subsampling: 444,
                                     algo: :jpeg_turbo,
                                     execution: :direct)
    assert_equal [[1, 1, 1], [2, 1, 1], [3, 1, 1]], sof_sampling(jpeg)
  end

  def test_auto_subsampling_uses_444_at_high_quality_for_size_profile
    jpeg = ImagePack.compress_pixels(sample_pixels,
                                     width: DEFAULT_WIDTH,
                                     height: DEFAULT_HEIGHT,
                                     channels: 3,
                                     quality: 95,
                                     algo: :mozjpeg,
                                     execution: :direct)
    assert_equal [[1, 1, 1], [2, 1, 1], [3, 1, 1]], sof_sampling(jpeg)
  end

  def test_fast_profile_keeps_420_at_high_quality
    jpeg = ImagePack.compress_pixels(sample_pixels,
                                     width: DEFAULT_WIDTH,
                                     height: DEFAULT_HEIGHT,
                                     channels: 3,
                                     quality: 95,
                                     algo: :jpeg_turbo,
                                     execution: :direct)
    assert_equal [[1, 2, 2], [2, 1, 1], [3, 1, 1]], sof_sampling(jpeg)
  end

  def test_auto_subsampling_uses_420_at_default_quality
    jpeg = ImagePack.compress_pixels(sample_pixels,
                                     width: DEFAULT_WIDTH,
                                     height: DEFAULT_HEIGHT,
                                     channels: 3,
                                     quality: 82,
                                     algo: :jpeg_turbo,
                                     execution: :direct)
    assert_equal [[1, 2, 2], [2, 1, 1], [3, 1, 1]], sof_sampling(jpeg)
  end

  def test_scale_half_halves_dimensions
    input = sample_jpeg(width: 96, height: 80)
    out = ImagePack.compress_bytes(input, quality: 82, scale: 0.5, algo: :jpeg_turbo,
                                          execution: :direct)
    info = ImagePack.inspect_image(out)
    assert_in_delta 48, info[:width], 8
    assert_in_delta 40, info[:height], 8
  end

  def test_optimize_applies_exif_orientation_when_stripping
    input = jpeg_with_exif_orientation(sample_jpeg(width: 96, height: 80), 6)
    out = ImagePack.optimize_bytes(input, strip_metadata: true, progressive: false,
                                          execution: :direct)
    assert_jpeg out
    info = ImagePack.inspect_image(out)
    assert_equal 80, info[:width]
    assert_equal 96, info[:height]
    refute out.include?("Exif")
  end

  def test_tune_and_effort_are_accepted
    jpeg = ImagePack.compress_pixels(sample_pixels,
                                     width: DEFAULT_WIDTH,
                                     height: DEFAULT_HEIGHT,
                                     channels: 3,
                                     tune: :ssim,
                                     effort: :fast,
                                     algo: :mozjpeg,
                                     execution: :direct)
    assert_jpeg jpeg
  end

  def test_rejects_unknown_subsampling_tune_effort_scale
    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.compress_pixels(sample_pixels, width: DEFAULT_WIDTH, height: DEFAULT_HEIGHT,
                                channels: 3, subsampling: :"410")
    end
    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.compress_pixels(sample_pixels, width: DEFAULT_WIDTH, height: DEFAULT_HEIGHT,
                                channels: 3, tune: :butteraugli)
    end
    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.compress_pixels(sample_pixels, width: DEFAULT_WIDTH, height: DEFAULT_HEIGHT,
                                channels: 3, effort: :ludicrous)
    end
    assert_raises(ImagePack::InvalidArgumentError) do
      ImagePack.compress(sample_jpeg, scale: 0.3, execution: :direct)
    end
  end

  def test_mozjpeg_trellis_false_still_encodes
    jpeg = ImagePack.compress_pixels(sample_pixels,
                                     width: DEFAULT_WIDTH,
                                     height: DEFAULT_HEIGHT,
                                     channels: 3,
                                     mozjpeg_trellis: false,
                                     algo: :mozjpeg,
                                     execution: :direct)
    assert_jpeg jpeg
  end

  private

  def progressive_jpeg?(bytes)
    i = 2
    while i + 3 < bytes.bytesize
      i += 1 while i < bytes.bytesize && bytes.getbyte(i) != 0xFF
      i += 1 while i < bytes.bytesize && bytes.getbyte(i) == 0xFF
      marker = bytes.getbyte(i)
      return true if marker == 0xC2
      return false if marker == 0xC0 || marker == 0xD9 || marker == 0xDA || marker.nil?
      return false if i + 2 >= bytes.bytesize

      length = (bytes.getbyte(i + 1) << 8) + bytes.getbyte(i + 2)
      return false if length < 2

      i += 1 + length
    end
    false
  end

  def sof_sampling(bytes)
    i = 2
    while i + 8 < bytes.bytesize
      i += 1 while i < bytes.bytesize && bytes.getbyte(i) != 0xFF
      i += 1 while i < bytes.bytesize && bytes.getbyte(i) == 0xFF
      marker = bytes.getbyte(i)
      if marker == 0xC0 || marker == 0xC2
        nf = bytes.getbyte(i + 8)
        return Array.new(nf) do |ci|
          id = bytes.getbyte(i + 9 + (ci * 3))
          hv = bytes.getbyte(i + 10 + (ci * 3))
          [id, hv >> 4, hv & 0xf]
        end
      end
      return nil if marker.nil? || marker == 0xD9 || marker == 0xDA
      return nil if i + 2 >= bytes.bytesize

      length = (bytes.getbyte(i + 1) << 8) + bytes.getbyte(i + 2)
      return nil if length < 2

      i += 1 + length
    end
    nil
  end

  def jpeg_with_icc(jpeg)
    payload = "ICC_PROFILE\0\x01\x01".b + ("P3" * 32).b
    app2 = [0xFF, 0xE2, (payload.bytesize + 2) >> 8, (payload.bytesize + 2) & 0xFF].pack("C*") + payload
    jpeg.dup.insert(2, app2)
  end

  def jpeg_with_exif_orientation(jpeg, orientation)
    tiff = +"II".b
    tiff << [0x002A, 8].pack("vV")
    tiff << [1].pack("v")
    tiff << [0x0112, 3, 1, orientation].pack("vvVV")
    tiff << [0].pack("V")
    payload = +"Exif\0\0".b + tiff
    app1 = [0xFF, 0xE1, (payload.bytesize + 2) >> 8, (payload.bytesize + 2) & 0xFF].pack("C*") + payload
    jpeg.dup.insert(2, app1)
  end

  def marker_bomb(jpeg, count:)
    payload = "x"
    com = [0xFF, 0xFE, 0x00, 0x03, payload.ord].pack("C*")
    jpeg.dup.insert(2, com * count)
  end
end
