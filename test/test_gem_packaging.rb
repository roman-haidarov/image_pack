# frozen_string_literal: true

require "minitest/autorun"

require_relative "../ext/image_pack/mozjpeg_sources"

class TestGemPackaging < Minitest::Test
  ROOT = File.expand_path("..", __dir__)
  MOZJPEG_DIR = "ext/image_pack/vendor/mozjpeg"

  FORBIDDEN_VENDOR_SOURCES = %w[
    cjpeg.c
    djpeg.c
    jpegtran.c
    rdgif.c
    rdpng.c
    rdbmp.c
    rdtarga.c
    turbojpeg.c
    turbojpeg-jni.c
    tjbench.c
    tjunittest.c
  ].freeze

  def test_gemspec_declares_ruby_2_7_1_as_the_minimum_runtime
    assert_equal Gem::Requirement.new(">= 2.7.1"), gemspec.required_ruby_version
  end

  def test_gemspec_packages_single_source_of_truth_and_runtime_c_files
    spec = gemspec

    assert_includes spec.files, "ext/image_pack/mozjpeg_sources.rb"

    ImagePackMozjpegSources::GEM_RUNTIME_C_SOURCES.each do |relative_path|
      path = File.join(MOZJPEG_DIR, relative_path)
      assert File.file?(File.join(ROOT, path)), "missing vendored #{path}"
      assert_includes spec.files, path, "#{path} must be packaged in the built gem"
    end
  end

  def test_packaged_scalar_c_include_templates_are_closed
    spec = gemspec

    ImagePackMozjpegSources::TOPLEVEL_C_SOURCES.each do |source|
      source_path = File.join(ROOT, MOZJPEG_DIR, source)
      next unless File.file?(source_path)

      File.binread(source_path).force_encoding(Encoding::BINARY).scan(/^\s*#\s*include\s+"([^"]+\.c)"/).flatten.each do |included|
        packaged_path = File.join(MOZJPEG_DIR, included)
        assert_includes spec.files, packaged_path, "#{source} includes #{included}, so #{packaged_path} must be packaged"
      end
    end
  end

  def test_gem_packages_x86_64_asm_sources
    spec = gemspec

    ImagePackMozjpegSources::X86_64_ASM_SOURCES.each do |relative_path|
      path = File.join(MOZJPEG_DIR, relative_path)
      assert File.file?(File.join(ROOT, path)), "missing vendored #{path}"
      assert_includes spec.files, path, "#{path} must be packaged so x86_64 builds can enable SIMD"
    end
  end

  def test_gem_does_not_package_unused_simd_architectures
    spec = gemspec
    unused = spec.files.select do |path|
      path.include?("/simd/i386/") ||
        path.include?("/simd/mips/") ||
        path.include?("/simd/mips64/") ||
        path.include?("/simd/powerpc/") ||
        path.include?("/aarch32/")
    end
    assert_empty unused, "unused SIMD trees must not be packaged: #{unused.first(5).inspect}"
  end

  def test_gem_does_not_package_known_mozjpeg_cli_or_test_sources
    spec = gemspec

    FORBIDDEN_VENDOR_SOURCES.each do |filename|
      refute_includes spec.files, File.join(MOZJPEG_DIR, filename)
    end
  end

  private

  def gemspec
    @gemspec ||= Gem::Specification.load(File.join(ROOT, "image_pack.gemspec"))
  end
end
