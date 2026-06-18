# frozen_string_literal: true

# Data-driven tuning harness for image_pack.
#
# Sweeps the encoder knobs reachable from the public API
# (algo x quality x progressive x mozjpeg_trellis) over one or
# more JPEG inputs, and reports bytes and median encode time per configuration.
#
# Why this exists: compression "improvements" should be proven, not guessed.
# Enabling a knob can cost a lot of CPU for little (or negative) byte savings,
# and the right setting is content-dependent (photo vs screenshot/graphics).
# This script makes that trade-off measurable before changing any defaults.
#
# Usage:
#   ruby benchmark/tuning_matrix.rb [image1.jpg image2.jpg ...]
#
# With no arguments it generates a synthetic photo-like and graphics-like seed
# so the harness runs out of the box.
#
# Reading the output:
#   * progressive changes scan layout and entropy optimization. Compare bytes and
#     time against the baseline profile before changing defaults.
#   * mozjpeg_trellis shifts quantization slightly, so it can change quality a
#     little; confirm trellis rows with `min_ssim:`/`report:` before trusting the
#     byte delta as a pure win.

require_relative "../lib/image_pack"

QUALITIES = (ENV["QUALITIES"] || "70,82,90").split(",").map(&:to_i)
ITERS = (ENV["ITERS"] || "7").to_i

# (algo, progressive, mozjpeg_trellis)
MATRIX = [
  [:fast, false, false],
  [:fast, true,  false],
  [:size, false, true],
  [:size, true,  true],
  [:size, false, false],
  [:size, true,  false],
].freeze

def median(values)
  sorted = values.sort
  sorted[sorted.length / 2]
end

def synthetic_seed(kind)
  w = 384
  h = 384
  buf = +"".b
  h.times do |y|
    w.times do |x|
      if kind == :graphics
        if ((x / 12) + (y / 12)).even?
          buf << 245 << 245 << 245
        else
          buf << 20 << 70 << 200
        end
      else # photo-like: smooth gradients plus light noise
        n = ((x * 131 + y * 57) % 23) - 11
        buf << ((x * 255 / w) + n).clamp(0, 255)
        buf << ((y * 255 / h) + n).clamp(0, 255)
        buf << (((x + y) * 255 / (w + h)) + n).clamp(0, 255)
      end
    end
  end
  # Seed JPEG the rest of the run decodes from, so every config starts equal.
  ImagePack.compress_pixels(buf, width: w, height: h, channels: 3, algo: :fast, quality: 95)
end

def load_inputs
  if ARGV.empty?
    warn "No image paths given; using synthetic photo-like and graphics-like seeds.\n\n"
    { "synthetic_photo" => synthetic_seed(:photo),
      "synthetic_graphics" => synthetic_seed(:graphics) }
  else
    ARGV.each_with_object({}) do |path, acc|
      acc[File.basename(path)] = File.binread(path)
    end
  end
end

def measure(jpeg, algo:, quality:, progressive:, mozjpeg_trellis:)
  opts = { algo: algo, quality: quality, progressive: progressive,
           mozjpeg_trellis: mozjpeg_trellis, strip_metadata: true }
  out = ImagePack.compress(jpeg, **opts)
  times = Array.new(ITERS) do
    t0 = Process.clock_gettime(Process::CLOCK_MONOTONIC)
    out = ImagePack.compress(jpeg, **opts)
    (Process.clock_gettime(Process::CLOCK_MONOTONIC) - t0) * 1000.0
  end
  [out.bytesize, median(times)]
end

def label(algo, prog, trellis)
  parts = [algo.to_s]
  parts << "progressive" if prog
  parts << "baseline" unless prog
  parts << "no-trellis" if algo == :size && trellis == false
  parts.join("+")
end

puts "image_pack tuning matrix"
puts "build: #{ImagePack.build_info.slice(:version, :mozjpeg, :simd)}"
puts "qualities=#{QUALITIES.inspect} iters=#{ITERS}"
puts

load_inputs.each do |name, jpeg|
  puts "== #{name} (#{jpeg.bytesize} input bytes) =="
  QUALITIES.each do |q|
    puts "  q=#{q}"
    rows = MATRIX.map do |algo, prog, trellis|
      bytes, ms = measure(jpeg, algo: algo, quality: q, progressive: prog,
                                 mozjpeg_trellis: trellis)
      { label: label(algo, prog, trellis), bytes: bytes, ms: ms }
    end
    smallest = rows.min_by { |r| r[:bytes] }[:bytes]
    fastest = rows.min_by { |r| r[:ms] }[:ms]
    rows.each do |r|
      flags = []
      flags << "smallest" if r[:bytes] == smallest
      flags << "fastest" if r[:ms] == fastest
      printf("    %-26s %8d B  %7.2f ms  %s\n",
             r[:label], r[:bytes], r[:ms], flags.join(", "))
    end
  end
  puts
end

puts "Note: progressive changes scan layout and CPU cost; verify profile changes with real inputs."
puts "      trellis shifts quantization slightly; verify trellis rows with min_ssim:/report:."
