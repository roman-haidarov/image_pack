# frozen_string_literal: true

# Native hot-path sample for:
#   ImagePack.compress(bytes, algo: :mozjpeg, quality: 82, mozjpeg_trellis: ENV.fetch("MOZJPEG_TRELLIS", "1") != "0")
#
# Run without arguments:
#   bundle exec ruby samples/mozjpeg_compress_hot_path.rb
#
# Default input:
#   tmp/cosmos.jpeg if present, otherwise /tmp/cosmos.jpeg

$stdout.sync = true
$stderr.sync = true
$LOAD_PATH.unshift(File.expand_path("../lib", __dir__))

require "image_pack"

input_path = ARGV[0] || (File.file?("tmp/cosmos.jpeg") ? "tmp/cosmos.jpeg" : "/tmp/cosmos.jpeg")
algo = :mozjpeg
quality = 82
mozjpeg_trellis = ENV.fetch("MOZJPEG_TRELLIS", "1") != "0"
sleep_before_hot_loop = 7.0
duration = 25.0
preheat_iterations = 3
sample_name = "image_pack_mozjpeg_compress_hot_path"
native_grep = "ImagePack|image_pack|ip_|jpeg_|jinit_|jcopy_|jzero_|jsimd_|huff|dct|jcap|jdat|jdap|jdcoef|jccoef|jchuff|jdhuff|jfdct|jidct|jmem|jutils|progression|optimize"

unless File.file?(input_path)
  abort "input image not found: #{input_path}\nput example image here: tmp/cosmos.jpeg or /tmp/cosmos.jpeg\nor pass path explicitly: bundle exec ruby samples/mozjpeg_compress_hot_path.rb photo.jpg"
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

preheat_iterations.times do
  out = ImagePack.compress(bytes, algo: algo, quality: quality, mozjpeg_trellis: mozjpeg_trellis)
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
puts "call=ImagePack.compress(bytes, algo: :#{algo}, quality: #{quality}, mozjpeg_trellis: #{mozjpeg_trellis})"
puts "algo=#{algo}"
puts "quality=#{quality}"
puts "mozjpeg_trellis=#{mozjpeg_trellis}"
puts "duration=#{duration}"
puts "preheat_iterations=#{preheat_iterations}"
puts "sample_seconds=#{sample_seconds}"
puts "sample_file=#{sample_file}"
puts "txt_file=#{txt_file}"
puts
puts "Copy this one-line capture command:"
puts %(mkdir -p "#{txt_dir}"; OUT="#{txt_file}"; SAMPLE="#{sample_file}"; { sample #{Process.pid} #{sample_seconds} -f "$SAMPLE"; echo; echo "===== focused native symbols ====="; filtercalltree "$SAMPLE" | grep -E "#{native_grep}" | head -260; echo; echo "===== filtercalltree head -260 ====="; filtercalltree "$SAMPLE" | head -260; } 2>&1 | tee "$OUT")
puts
puts "Expected hot native symbols:"
puts "  ImagePack.compress -> __compress_jpeg"
puts "  ip_compress_jpeg_entry / ip_mozjpeg_compress / compress_jpeg_input_with_mode"
puts "  jpeg_read_header / jpeg_start_decompress / jpeg_read_scanlines / jpeg_finish_decompress"
puts "  jpeg_simple_progression / optimized Huffman coding path"
puts "  quantize_trellis when MOZJPEG_TRELLIS=1; should disappear or drop when MOZJPEG_TRELLIS=0"
puts "  jpeg_set_quality / jpeg_start_compress / jpeg_write_scanlines / jpeg_finish_compress"
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
    last_output = ImagePack.compress(bytes, algo: algo, quality: quality, mozjpeg_trellis: mozjpeg_trellis)
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
