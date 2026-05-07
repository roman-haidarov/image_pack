# frozen_string_literal: true

require "image_pack"

# path = ARGV.fetch(0) { abort "usage: ruby benchmark/batch_threads.rb photo.jpg [threads] [iterations]" }
path = "tmp/cosmos.jpeg"
threads = Integer(ARGV.fetch(1, 4))
iterations = Integer(ARGV.fetch(2, 20))
bytes = File.binread(path)

started = Process.clock_gettime(Process::CLOCK_MONOTONIC)
threads.times.map do
  Thread.new do
    iterations.times do
      ImagePack.compress(bytes, algo: :mozjpeg, quality: 82, execution: :nogvl)
    end
  end
end.each(&:join)
finished = Process.clock_gettime(Process::CLOCK_MONOTONIC)

puts "threads=#{threads} iterations=#{iterations} seconds=#{(finished - started).round(3)}"
