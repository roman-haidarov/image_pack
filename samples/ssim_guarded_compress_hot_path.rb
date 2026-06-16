# frozen_string_literal: true

# Native hot-path sample for the SSIM-guarded compression path:
#   ImagePack.compress(bytes, algo: :jpeg_turbo, min_ssim: 0.985)
#
# Run without arguments:
#   bundle exec ruby samples/ssim_guarded_compress_hot_path.rb
#
# Default input:
#   tmp/cosmos.jpeg if present, otherwise /tmp/cosmos.jpeg
#
# Optional env:
#   ALGO=mozjpeg MIN_SSIM=0.985 QUALITY=75 DURATION=25 PREHEAT_ITERATIONS=1 \
#     bundle exec ruby samples/ssim_guarded_compress_hot_path.rb photo.jpg
#
# QUALITY is intentionally optional:
#   - without QUALITY, this exercises the full auto-quality path
#   - with QUALITY, the native path starts there and raises quality if SSIM is too low

$stdout.sync = true
$stderr.sync = true
$LOAD_PATH.unshift(File.expand_path("../lib", __dir__))

require "image_pack"

input_path = ARGV[0] || (File.file?("tmp/cosmos.jpeg") ? "tmp/cosmos.jpeg" : "/tmp/cosmos.jpeg")
algo = ENV.fetch("ALGO", "jpeg_turbo").to_sym
min_ssim = Float(ENV.fetch("MIN_SSIM", "0.985"))
quality = ENV["QUALITY"]&.then { |value| Integer(value) }
sleep_before_hot_loop = Float(ENV.fetch("SLEEP_BEFORE_HOT_LOOP", "7.0"))
duration = Float(ENV.fetch("DURATION", "25.0"))
preheat_iterations = Integer(ENV.fetch("PREHEAT_ITERATIONS", "1"))
sample_name = "image_pack_ssim_guarded_compress_hot_path"
native_grep = "ImagePack|image_pack|ip_|guarded|ssim|luma|jpeg_|jinit_|jcopy_|jzero_|jsimd_|huff|dct|jcap|jdat|jdap|jdcoef|jccoef|jchuff|jdhuff|jfdct|jidct|jmem|jutils|progression|optimize"

unless File.file?(input_path)
  abort "input image not found: #{input_path}\nput example image here: tmp/cosmos.jpeg or /tmp/cosmos.jpeg\nor pass path explicitly: bundle exec ruby samples/ssim_guarded_compress_hot_path.rb photo.jpg"
end

bytes = File.binread(input_path).b.freeze
raise "input is empty: #{input_path}" if bytes.empty?

def monotonic
  Process.clock_gettime(Process::CLOCK_MONOTONIC)
end

def gc_snapshot
  stat = GC.stat

  {
    total_allocated_objects: stat.fetch(:total_allocated_objects),
    minor_gc_count: stat.fetch(:minor_gc_count),
    major_gc_count: stat.fetch(:major_gc_count)
  }
end

def gc_delta(before, after)
  before.each_with_object({}) do |(key, value), out|
    out[key] = after.fetch(key) - value
  end
end

def jpeg_bytes!(out, label)
  raise "#{label}: expected binary String, got #{out.class}" unless out.is_a?(String)
  raise "#{label}: output is too small: #{out.bytesize}" if out.bytesize < 4
  raise "#{label}: missing JPEG SOI marker" unless out.getbyte(0) == 0xFF && out.getbyte(1) == 0xD8
  raise "#{label}: missing JPEG EOI marker" unless out.getbyte(out.bytesize - 2) == 0xFF && out.getbyte(out.bytesize - 1) == 0xD9
end

def compress_guarded(bytes, algo:, quality:, min_ssim:)
  opts = { algo: algo, min_ssim: min_ssim }
  opts[:quality] = quality unless quality.nil?

  ImagePack.compress(bytes, **opts)
end

def call_description(algo, quality, min_ssim)
  if quality.nil?
    "ImagePack.compress(bytes, algo: :#{algo}, min_ssim: #{min_ssim})"
  else
    "ImagePack.compress(bytes, algo: :#{algo}, quality: #{quality}, min_ssim: #{min_ssim})"
  end
end

preheat_iterations.times do
  out = compress_guarded(bytes, algo: algo, quality: quality, min_ssim: min_ssim)
  jpeg_bytes!(out, "preheat")
end

sample_seconds = (sleep_before_hot_loop + duration + 15).ceil
sample_file = "/tmp/#{sample_name}.sample"
txt_file = File.expand_path("results/#{sample_name}.txt", __dir__)
txt_dir = File.dirname(txt_file)

puts "pid=#{Process.pid}"
puts "ruby=#{RUBY_DESCRIPTION}"
puts "platform=#{RUBY_PLATFORM}"
puts "mode=#{sample_name}"
puts "input_path=#{input_path}"
puts "input_bytes=#{bytes.bytesize}"
puts "call=#{call_description(algo, quality, min_ssim)}"
puts "algo=#{algo}"
puts "quality=#{quality.nil? ? 'auto' : quality}"
puts "min_ssim=#{min_ssim}"
puts "duration=#{duration}"
puts "preheat_iterations=#{preheat_iterations}"
puts "sample_seconds=#{sample_seconds}"
puts "sample_file=#{sample_file}"
puts "txt_file=#{txt_file}"
puts
puts "Copy this one-line capture command:"
ssim_grep = "guarded|ssim|luma|ip_build_luma_buffer|ip_decode_jpeg_to_luma_buffer|ip_compute_ssim_luma_buffer"
encode_grep = "encode_pixels_with_libjpeg|jpeg_write_scanlines|jpeg_finish_compress|jchuff|jfdct|quantize|jsimd_"
decode_grep = "ip_jpeg_decode_to_pixels|jpeg_read_header|jpeg_start_decompress|jpeg_read_scanlines|jpeg_finish_decompress|jdhuff|jidct|jdcoef|jsimd_"
puts %(mkdir -p "#{txt_dir}"; OUT="#{txt_file}"; SAMPLE="#{sample_file}"; { sample #{Process.pid} #{sample_seconds} -f "$SAMPLE"; echo; echo "===== SSIM/luma focused symbols ====="; filtercalltree "$SAMPLE" | grep -E "#{ssim_grep}" | head -220; echo; echo "===== encode focused symbols ====="; filtercalltree "$SAMPLE" | grep -E "#{encode_grep}" | head -220; echo; echo "===== decode focused symbols ====="; filtercalltree "$SAMPLE" | grep -E "#{decode_grep}" | head -220; echo; echo "===== broad native symbols ====="; filtercalltree "$SAMPLE" | grep -E "#{native_grep}" | head -320; echo; echo "===== filtercalltree head -320 ====="; filtercalltree "$SAMPLE" | head -320; } 2>&1 | tee "$OUT")
puts
puts "Expected hot native symbols:"
puts "  ImagePack.compress -> __compress_jpeg"
puts "  ip_compress_jpeg_entry / ip_jpeg_turbo_compress or ip_mozjpeg_compress"
puts "  guarded_compress_jpeg_input_with_mode"
puts "  ip_jpeg_decode_to_pixels / encode_pixels_with_libjpeg"
puts "  ip_build_luma_buffer"
puts "  ip_compute_ssim_luma_buffer"
puts "  repeated jpeg_read_header / jpeg_start_decompress / jpeg_read_scanlines / jpeg_finish_decompress"
puts "  repeated jpeg_set_quality / jpeg_start_compress / jpeg_write_scanlines / jpeg_finish_compress"
puts "  jdhuff/jchuff, jidct/jfdct, jmemmgr, jsimd_none or platform SIMD symbols"
puts
puts "sleep=#{sleep_before_hot_loop} seconds before hot loop"
puts

begin
  GC.start
  GC.disable
  before_gc = gc_snapshot
  sleep sleep_before_hot_loop

  count = 0
  first_output_bytes = nil
  started = monotonic
  deadline = started + duration

  last_output = nil

  while monotonic < deadline
    last_output = compress_guarded(bytes, algo: algo, quality: quality, min_ssim: min_ssim)
    first_output_bytes ||= last_output.bytesize
    count += 1
  end

  jpeg_bytes!(last_output, "last_hot_loop") if last_output

  elapsed = monotonic - started
  after_gc = gc_snapshot

  puts "count=#{count}"
  puts "elapsed=#{format('%.6f', elapsed)}"
  puts "ops_per_sec=#{format('%.6f', count / elapsed)}"
  puts "sec_per_op=#{format('%.6f', elapsed / [count, 1].max)}"
  puts "first_output_bytes=#{first_output_bytes}"
  puts "gc_delta=#{gc_delta(before_gc, after_gc)}"
ensure
  GC.enable
end
