#!/usr/bin/env ruby
# frozen_string_literal: true

# Usage:
#   ruby script/vendor_libs.rb
#   rake compile
#   gem build image_pack.gemspec

require "digest"
require "etc"
require "fileutils"
require "open-uri"
require "open3"
require "tmpdir"

require_relative "../ext/image_pack/mozjpeg_sources"

VENDOR_DIR = File.expand_path("../ext/image_pack/vendor", __dir__)

MOZJPEG = {
  version: ImagePackMozjpegSources::VERSION,
  url:     "https://github.com/mozilla/mozjpeg/archive/refs/tags/v%<version>s.tar.gz",
  sha256:  ImagePackMozjpegSources::SHA256,
  strip:   "mozjpeg-%<version>s",
}.freeze

JPEGLI = {
  version: "0.11.2",
  repo:    "https://github.com/libjxl/libjxl.git",
  ref:     ENV.fetch("IMAGE_PACK_JPEGLI_REF", "v0.11.2"),
}.freeze

SKIP_JPEGLI = ENV.fetch("IMAGE_PACK_VENDOR_JPEGLI", "0") != "1"

def run!(*cmd, chdir: nil)
  puts "  $ #{cmd.join(' ')}"
  stdout, stderr, status = Open3.capture3(*cmd, chdir: chdir)
  puts stdout unless stdout.empty?
  warn stderr unless stderr.empty?
  abort "command failed: #{cmd.join(' ')}" unless status.success?
end

def command!(name)
  ENV.fetch("PATH", "").split(File::PATH_SEPARATOR).each do |dir|
    path = File.join(dir, name)
    return path if File.file?(path) && File.executable?(path)
  end

  abort "required command not found on PATH: #{name}"
end

def download(url, dest)
  puts "  Downloading #{url}..."
  URI.open(url) { |remote| File.binwrite(dest, remote.read) }
end

def verify_checksum!(tarball, name, expected_sha256)
  actual = Digest::SHA256.file(tarball).hexdigest

  if expected_sha256
    abort "SHA256 mismatch for #{name}! Expected #{expected_sha256}, got #{actual}" unless actual == expected_sha256
    puts "  SHA256 verified."
  else
    puts "  SHA256: #{actual} (pin this in script/vendor_libs.rb)"
  end
end

def mozjpeg_file?(relative_path)
  ImagePackMozjpegSources.mozjpeg_file?(relative_path)
end

def extract_mozjpeg(tarball, dest, strip_prefix:)
  require "rubygems/package"
  require "zlib"

  puts "  Extracting whitelisted MozJPEG runtime sources (incl. SIMD)..."
  FileUtils.mkdir_p(dest)

  prefix_re = /\A#{Regexp.escape(strip_prefix)}\//
  count_top = 0
  count_simd = 0

  Gem::Package::TarReader.new(Zlib::GzipReader.open(tarball)) do |tar|
    tar.each do |entry|
      relative_path = entry.full_name.sub(prefix_re, "")
      next if relative_path == entry.full_name
      next unless entry.file?
      next unless mozjpeg_file?(relative_path)

      target = File.join(dest, relative_path)
      FileUtils.mkdir_p(File.dirname(target))
      File.binwrite(target, entry.read)

      if relative_path.start_with?("simd/")
        count_simd += 1
      else
        count_top += 1
      end
    end
  end

  puts "  -> #{count_top} top-level + #{count_simd} simd/ files"
end

def write_manifest!(mozjpeg_version, jpegli_version: nil, jpegli_ref: nil)
  lines = ["mozjpeg=#{mozjpeg_version}"]
  lines << "jpegli=#{jpegli_version}" if jpegli_version
  lines << "jpegli_ref=#{jpegli_ref}" if jpegli_ref
  File.write(File.join(VENDOR_DIR, ".vendored"), lines.join("\n") + "\n")
end

def vendor_mozjpeg!(tmpdir)
  version = MOZJPEG.fetch(:version)
  url     = format(MOZJPEG.fetch(:url), version: version)
  strip   = format(MOZJPEG.fetch(:strip), version: version)
  tarball = File.join(tmpdir, "mozjpeg-#{version}.tar.gz")
  dest    = File.join(VENDOR_DIR, "mozjpeg")

  puts "=== mozjpeg #{version} ==="

  download(url, tarball)
  verify_checksum!(tarball, :mozjpeg, MOZJPEG[:sha256])
  extract_mozjpeg(tarball, dest, strip_prefix: strip)

  puts "  -> #{dest}"
  puts
end

def find_cjpegli!(build_dir)
  candidates = Dir[
    File.join(build_dir, "tools", "cjpegli"),
    File.join(build_dir, "tools", "cjpegli.exe"),
    File.join(build_dir, "**", "cjpegli"),
    File.join(build_dir, "**", "cjpegli.exe")
  ].select { |path| File.file?(path) && File.executable?(path) }

  abort "cjpegli was not produced by the jpegli build" if candidates.empty?
  candidates.first
end

def vendor_jpegli!(tmpdir)
  return if SKIP_JPEGLI

  command!("git")
  command!("cmake")

  version = JPEGLI.fetch(:version)
  ref = JPEGLI.fetch(:ref)
  repo = JPEGLI.fetch(:repo)
  src = File.join(tmpdir, "libjxl")
  build = File.join(tmpdir, "libjxl-build")
  dest = File.join(VENDOR_DIR, "jpegli")
  bin_dir = File.join(dest, "bin")

  puts "=== jpegli #{version} (#{ref}) ==="
  run!("git", "clone", "--depth", "1", "--branch", ref,
       "--recursive", "--shallow-submodules", repo, src)

  cmake_args = [
    "cmake", "-S", src, "-B", build,
    "-DCMAKE_BUILD_TYPE=Release",
    "-DBUILD_TESTING=OFF",
    "-DJPEGXL_ENABLE_BENCHMARK=OFF",
    "-DJPEGXL_ENABLE_DEVTOOLS=OFF",
    "-DJPEGXL_ENABLE_DOXYGEN=OFF",
    "-DJPEGXL_ENABLE_EXAMPLES=OFF",
    "-DJPEGXL_ENABLE_JNI=OFF",
    "-DJPEGXL_ENABLE_MANPAGES=OFF",
    "-DJPEGXL_ENABLE_PLUGINS=OFF",
    "-DJPEGXL_ENABLE_TOOLS=ON"
  ]
  run!(*cmake_args)
  run!("cmake", "--build", build, "--target", "cjpegli", "--parallel", Etc.nprocessors.to_s)

  cjpegli = find_cjpegli!(build)
  FileUtils.mkdir_p(bin_dir)
  FileUtils.cp(cjpegli, File.join(bin_dir, File.basename(cjpegli)))
  FileUtils.chmod(0o755, File.join(bin_dir, File.basename(cjpegli)))
  File.write(File.join(dest, "VERSION"), "#{version}\nref=#{ref}\nrepo=#{repo}\n")

  puts "  -> #{File.join(bin_dir, File.basename(cjpegli))}"
  puts
end

puts "Vendoring native libraries into #{VENDOR_DIR}"
puts

FileUtils.rm_rf(VENDOR_DIR)
FileUtils.mkdir_p(VENDOR_DIR)

tmpdir = File.join(Dir.tmpdir, "image_pack-vendor-#{$$}")
FileUtils.mkdir_p(tmpdir)

begin
  vendor_mozjpeg!(tmpdir)
  vendor_jpegli!(tmpdir)
  write_manifest!(MOZJPEG.fetch(:version),
                  jpegli_version: (SKIP_JPEGLI ? nil : JPEGLI.fetch(:version)),
                  jpegli_ref: (SKIP_JPEGLI ? nil : JPEGLI.fetch(:ref)))

  puts "Done! Vendored files are in ext/image_pack/vendor/"
  puts "Now run: gem build image_pack.gemspec"
ensure
  FileUtils.rm_rf(tmpdir)
end
