# frozen_string_literal: true

# Native hot-path sample for coefficient-level lossless JPEG optimize:
#   ImagePack.optimize_jpeg(bytes, progressive: true, strip_metadata: false)
#
# Run without arguments:
#   bundle exec ruby samples/lossless_optimize_hot_path.rb
#
# Default input:
#   tmp/cosmos.jpeg if present, otherwise /tmp/cosmos.jpeg

$stdout.sync = true
$stderr.sync = true
$LOAD_PATH.unshift(File.expand_path("../lib", __dir__))

require "image_pack"

input_path = ARGV[0] || (File.file?("tmp/cosmos.jpeg") ? "tmp/cosmos.jpeg" : "/tmp/cosmos.jpeg")
progressive = ENV.fetch("PROGRESSIVE", "true") == "true"
strip_metadata = ENV.fetch("STRIP_METADATA", "false") == "true"
sleep_before_hot_loop = Float(ENV.fetch("SLEEP_BEFORE_HOT_LOOP", "7.0"))
duration = Float(ENV.fetch("DURATION", "25.0"))
preheat_iterations = Integer(ENV.fetch("PREHEAT_ITERATIONS", "3"))
sample_name = "image_pack_lossless_optimize_hot_path"
native_grep = "ImagePack|image_pack|ip_|optimize|coeff|jpeg_read_coefficients|jpeg_write_coefficients|jpeg_copy_critical_parameters|jpeg_finish_compress|jpeg_finish_decompress|jdcoef|jccoef|jchuff|jdhuff|jmem|jutils|progression"

unless File.file?(input_path)
  abort "input image not found: #{input_path}\nput example image here: tmp/cosmos.jpeg or /tmp/cosmos.jpeg\nor pass path explicitly: bundle exec ruby samples/lossless_optimize_hot_path.rb photo.jpg"
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
  before.each_with_object({}) { |(key, value), out| out[key] = after.fetch(key) - value }
end

def jpeg_bytes!(out, label)
  raise "#{label}: expected binary String, got #{out.class}" unless out.is_a?(String)
  raise "#{label}: output is too small: #{out.bytesize}" if out.bytesize < 4
  raise "#{label}: missing JPEG SOI marker" unless out.getbyte(0) == 0xFF && out.getbyte(1) == 0xD8
  raise "#{label}: missing JPEG EOI marker" unless out.getbyte(out.bytesize - 2) == 0xFF && out.getbyte(out.bytesize - 1) == 0xD9
end

preheat_iterations.times do
  out = ImagePack.optimize_jpeg(bytes, progressive: progressive, strip_metadata: strip_metadata)
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
puts "call=ImagePack.optimize_jpeg(bytes, progressive: #{progressive}, strip_metadata: #{strip_metadata})"
puts "progressive=#{progressive}"
puts "strip_metadata=#{strip_metadata}"
puts "duration=#{duration}"
puts "preheat_iterations=#{preheat_iterations}"
puts "sample_seconds=#{sample_seconds}"
puts "sample_file=#{sample_file}"
puts "txt_file=#{txt_file}"
puts
puts "Copy this one-line capture command:"
puts %(mkdir -p "#{txt_dir}"; OUT="#{txt_file}"; SAMPLE="#{sample_file}"; { sample #{Process.pid} #{sample_seconds} -f "$SAMPLE"; echo; echo "===== coefficient optimize native symbols ====="; filtercalltree "$SAMPLE" | grep -E "#{native_grep}" | head -300; echo; echo "===== filtercalltree head -260 ====="; filtercalltree "$SAMPLE" | head -260; } 2>&1 | tee "$OUT")
puts
puts "Expected hot native symbols:"
puts "  ImagePack.optimize_jpeg -> __optimize_jpeg"
puts "  ip_optimize_jpeg_entry / ip_lossless_optimize_jpeg"
puts "  jpeg_read_header / jpeg_read_coefficients / jpeg_copy_critical_parameters"
puts "  jpeg_write_coefficients / jpeg_finish_compress / jpeg_finish_decompress"
puts "  jdcoef/jccoef/jchuff/jdhuff/jmem symbols, without pixel decode/IDCT/fDCT hot path"
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
    last_output = ImagePack.optimize_jpeg(bytes, progressive: progressive, strip_metadata: strip_metadata)
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
