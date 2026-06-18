# frozen_string_literal: true

require "image_pack"

# path = ARGV.fetch(0) { abort "usage: ruby benchmark/rss_io_buffer.rb photo.jpg" }
path = "tmp/cosmos.jpeg"

def rss_kb
  `ps -o rss= -p #{Process.pid}`.to_i
end

before = rss_kb
bytes = File.binread(path)
ImagePack.compress(bytes, engine: :turbo)
after_string = rss_kb

if defined?(IO::Buffer)
  File.open(path, "rb") do |file|
    buffer = IO::Buffer.map(file)
    ImagePack.compress(buffer, engine: :turbo)
  end
end
after_buffer = rss_kb

puts "rss_before_kb=#{before} rss_after_string_kb=#{after_string} rss_after_io_buffer_kb=#{after_buffer}"
