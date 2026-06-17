#!/usr/bin/env ruby
# frozen_string_literal: true

# Usage:
#   ruby script/vendor_libs.rb
#   rake compile
#   gem build image_pack.gemspec

require "digest"
require "fileutils"
require "open-uri"
require "tmpdir"

require_relative "../ext/image_pack/mozjpeg_sources"

VENDOR_DIR = File.expand_path("../ext/image_pack/vendor", __dir__)

MOZJPEG = {
  version: ImagePackMozjpegSources::VERSION,
  url:     "https://github.com/mozilla/mozjpeg/archive/refs/tags/v%<version>s.tar.gz",
  sha256:  ImagePackMozjpegSources::SHA256,
  strip:   "mozjpeg-%<version>s",
}.freeze

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

def write_manifest!(version)
  File.write(File.join(VENDOR_DIR, ".vendored"), "mozjpeg=#{version}\n")
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

puts "Vendoring C libraries into #{VENDOR_DIR}"
puts

FileUtils.rm_rf(VENDOR_DIR)
FileUtils.mkdir_p(VENDOR_DIR)

tmpdir = File.join(Dir.tmpdir, "image_pack-vendor-#{$$}")
FileUtils.mkdir_p(tmpdir)

begin
  vendor_mozjpeg!(tmpdir)
  write_manifest!(MOZJPEG.fetch(:version))

  puts "Done! Vendored sources are in ext/image_pack/vendor/"
  puts "Now run: gem build image_pack.gemspec"
ensure
  FileUtils.rm_rf(tmpdir)
end
