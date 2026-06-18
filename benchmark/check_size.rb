# frozen_string_literal: true

require "image_pack"

path = ARGV[0] || (File.file?("tmp/cosmos.jpeg") ? "tmp/cosmos.jpeg" : "/tmp/cosmos.jpeg")
abort "input image not found: #{path}" unless File.file?(path)

bytes = File.binread(path)

[20, 50, 82, 95].each do |quality|
  out = ImagePack.compress(bytes, engine: :turbo, quality: quality)
  File.binwrite("tmp/turbo_q#{quality}.jpg", out)
  puts "turbo q=#{quality}: #{out.bytesize}"
end

[20, 50, 82, 95].each do |quality|
  out = ImagePack.compress(bytes, engine: :mozjpeg, quality: quality)
  File.binwrite("tmp/mozjpeg_q#{quality}.jpg", out)
  puts "mozjpeg q=#{quality}: #{out.bytesize}"
end
