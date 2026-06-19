# frozen_string_literal: true

require_relative "test_helper"

# Guards the SIMD detection plumbing that the release-check (rake simd:check)
# relies on. The goal is to make a silent scalar fallback impossible to ship by
# accident: build_info must always report SIMD as a real boolean, and the
# vendored SIMD sources must be present so a release build *can* enable it.
class TestBuildInfo < Minitest::Test
  ROOT = File.expand_path("..", __dir__)

  def test_build_info_exposes_simd_as_boolean
    simd = ImagePack.build_info[:simd]
    assert_includes [true, false], simd,
                    "build_info[:simd] must be true or false (got #{simd.inspect}); " \
                    "the release-check depends on this being a real boolean"
  end

  def test_native_simd_constant_is_defined
    assert ImagePack.const_defined?(:NATIVE_SIMD),
           "NATIVE_SIMD constant must be defined by the native extension"
    assert_includes [true, false], ImagePack::NATIVE_SIMD
  end

  def test_simd_constant_matches_build_info
    assert_equal ImagePack::NATIVE_SIMD, ImagePack.build_info[:simd]
  end

  def test_vendored_simd_sources_present_for_x86_64
    skip "x86_64-only check" unless RUBY_PLATFORM =~ /\A(?:x86_64|x64)/

    asm = Dir[File.join(ROOT, "ext/image_pack/vendor/mozjpeg/simd/x86_64/*.asm")]
    assert asm.length.positive?,
           "vendored x86_64 SIMD .asm sources must be present so release builds can enable SIMD"
  end
end
