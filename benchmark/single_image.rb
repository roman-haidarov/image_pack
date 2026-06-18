# frozen_string_literal: true

require "benchmark/ips"
require "image_pack"

path = ARGV[0] || (File.file?("tmp/cosmos.jpeg") ? "tmp/cosmos.jpeg" : "/tmp/cosmos.jpeg")
abort "input image not found: #{path}" unless File.file?(path)
bytes = File.binread(path)

Benchmark.ips do |x|
  x.report("jpegli") { ImagePack.compress(bytes, engine: :jpegli, quality: 82) } if ImagePack.jpegli_available?
  x.report("turbo") { ImagePack.compress(bytes, engine: :turbo, quality: 82) }
  x.report("mozjpeg") { ImagePack.compress(bytes, engine: :mozjpeg, quality: 82) }
  x.compare!
end
