# frozen_string_literal: true

require "async"
require "image_pack"

# path = ARGV.fetch(0) { abort "usage: ruby benchmark/batch_fibers.rb photo.jpg [fibers] [iterations]" }
path = "tmp/cosmos.jpeg"
fibers = Integer(ARGV.fetch(1, 10))
iterations = Integer(ARGV.fetch(2, 10))
bytes = File.binread(path)

started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
Async do |task|
  fibers.times do
    task.async do
      iterations.times do
        ImagePack.compress(bytes, engine: :mozjpeg, quality: 82, execution: :auto)
      end
    end
  end
end
finished = Process.clock_gettime(Process::CLOCK_MONOTONIC)

puts "fibers=#{fibers} iterations=#{iterations} seconds=#{(finished - started).round(3)}"
