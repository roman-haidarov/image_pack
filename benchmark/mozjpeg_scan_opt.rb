# frozen_string_literal: true

# Size<->speed trade-off for the MozJPEG progressive scan search.
#
# `algo: :mozjpeg` spends most of finish_compress in the progressive scan search
# (OPTIMIZE_SCANS): it builds a 64-trial-scan script and entropy-codes the whole
# image once per trial for a few percent of size. `mozjpeg_scan_opt: false` keeps
# trellis + progressive + Huffman optimization but emits the compact (~10-scan)
# progressive script without that search.
#
# This benchmark reports throughput (benchmark/ips) and output sizes for:
#   - mozjpeg              (full scan search, the default)
#   - mozjpeg no-scan-opt  (mozjpeg_scan_opt: false)
#   - jpeg_turbo           (baseline throughput floor, for reference)
#
# Run:
#   bundle exec ruby benchmark/mozjpeg_scan_opt.rb [path/to/image.jpg]
#
# Default input: tmp/cosmos.jpeg if present, otherwise /tmp/cosmos.jpeg

require "benchmark/ips"
require "image_pack"

path = ARGV[0] || (File.file?("tmp/cosmos.jpeg") ? "tmp/cosmos.jpeg" : "/tmp/cosmos.jpeg")
abort "input image not found: #{path}" unless File.file?(path)
bytes = File.binread(path)

quality = Integer(ENV.fetch("QUALITY", "82"))

puts "input=#{path} (#{bytes.bytesize} bytes), quality=#{quality}"
puts "ruby=#{RUBY_DESCRIPTION}"
puts

# Sizes first so the speed/size trade-off is visible in one run.
full      = ImagePack.compress(bytes, algo: :mozjpeg, quality: quality)
no_scan   = ImagePack.compress(bytes, algo: :mozjpeg, quality: quality, mozjpeg_scan_opt: false)
turbo     = ImagePack.compress(bytes, algo: :jpeg_turbo, quality: quality)

puts "sizes:"
puts "  mozjpeg (full scan search)   q=#{quality}: #{full.bytesize}"
puts format("  mozjpeg (scan_opt: false)    q=%d: %d  (%+0.2f%% vs full)",
            quality, no_scan.bytesize, 100.0 * (no_scan.bytesize - full.bytesize) / full.bytesize)
puts format("  jpeg_turbo                   q=%d: %d  (%+0.2f%% vs full)",
            quality, turbo.bytesize, 100.0 * (turbo.bytesize - full.bytesize) / full.bytesize)
puts

Benchmark.ips do |x|
  x.report("mozjpeg full")          { ImagePack.compress(bytes, algo: :mozjpeg, quality: quality) }
  x.report("mozjpeg no-scan-opt")   { ImagePack.compress(bytes, algo: :mozjpeg, quality: quality, mozjpeg_scan_opt: false) }
  x.report("jpeg_turbo")            { ImagePack.compress(bytes, algo: :jpeg_turbo, quality: quality) }
  x.compare!
end
