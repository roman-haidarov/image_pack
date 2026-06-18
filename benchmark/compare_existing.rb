# frozen_string_literal: true

require "benchmark/ips"

require "image_pack"

def generated_fixture
  width = Integer(ENV.fetch("BENCH_FIXTURE_WIDTH", "768"))
  height = Integer(ENV.fetch("BENCH_FIXTURE_HEIGHT", "512"))

  pixels = String.new(capacity: width * height * 3, encoding: Encoding::BINARY)

  height.times do |y|
    width.times do |x|
      r = x * 255 / [width - 1, 1].max
      g = y * 255 / [height - 1, 1].max
      b = (x ^ y) & 0xff

      pixels << r << g << b
    end
  end

  ImagePack.compress_pixels(
    pixels,
    width: width,
    height: height,
    channels: 3,
    engine: :turbo,
    quality: 95
  )
end

def jpeg_bytes?(output)
  output = output.to_s.b

  output.bytesize >= 4 &&
    output.start_with?("\xFF\xD8".b) &&
    output.end_with?("\xFF\xD9".b)
end

def candidate(name, candidates)
  output = yield
  output = output.to_s.b

  raise "#{name} returned empty output" if output.empty?
  raise "#{name} did not return complete JPEG bytes" unless jpeg_bytes?(output)

  candidates << [name, proc { yield }]
rescue LoadError, StandardError => e
  warn "skip #{name}: #{e.message.lines.first.to_s.strip}"
end

def image_optim_data(optimizer, bytes)
  output = optimizer.optimize_image_data(bytes)
  output || bytes
end

path = ARGV[0] || (File.file?("tmp/cosmos.jpeg") ? "tmp/cosmos.jpeg" : "/tmp/cosmos.jpeg")
quality = Integer(ARGV[1] || 82)

bytes =
  if File.file?(path)
    File.binread(path).b
  else
    warn "input image not found: #{path}; using generated synthetic JPEG fixture"
    generated_fixture
  end

raise "input image is empty" if bytes.empty?
raise "input image is not a complete JPEG" unless jpeg_bytes?(bytes)

info = ImagePack.inspect_image(bytes)
width = info[:width] || info["width"]
height = info[:height] || info["height"]

warn "benchmark input: #{width}x#{height}, #{bytes.bytesize} bytes, q=#{quality}"

candidates = []

candidate("image_pack turbo", candidates) do
  ImagePack.compress(bytes, engine: :turbo, quality: quality)
end

candidate("image_pack mozjpeg", candidates) do
  ImagePack.compress(bytes, engine: :mozjpeg, quality: quality)
end

begin
  require "vips"

  candidate("ruby-vips jpeg", candidates) do
    image = Vips::Image.new_from_buffer(bytes, "")
    image.write_to_buffer(".jpg[Q=#{quality}]")
  end
rescue LoadError, StandardError => e
  warn "skip ruby-vips: #{e.message.lines.first.to_s.strip}"
end

begin
  begin
    require "image_optim_pack"
  rescue LoadError
    nil
  end

  require "image_optim"

  jpegoptim = ImageOptim.new(
    advpng: false,
    gifsicle: false,
    jhead: false,
    jpegrecompress: false,
    jpegtran: false,
    optipng: false,
    pngcrush: false,
    pngout: false,
    pngquant: false,
    svgo: false,
    jpegoptim: {
      max_quality: quality,
      strip: :all
    }
  )

  candidate("image_optim jpegoptim", candidates) do
    image_optim_data(jpegoptim, bytes)
  end

  strong = ImageOptim.new(
    advpng: false,
    gifsicle: false,
    jhead: false,
    optipng: false,
    pngcrush: false,
    pngout: false,
    pngquant: false,
    svgo: false,
    jpegoptim: {
      max_quality: quality,
      strip: :all
    },
    jpegtran: {
      progressive: true,
      copy_chunks: false
    },
    jpegrecompress: {
      quality: quality
    }
  )

  candidate("image_optim strong", candidates) do
    image_optim_data(strong, bytes)
  end
rescue LoadError, StandardError => e
  warn "skip image_optim: #{e.message.lines.first.to_s.strip}"
end

abort "no benchmark candidates available" if candidates.empty?

warn "benchmark candidates: #{candidates.map(&:first).join(', ')}"

warmup = Float(ENV.fetch("IPS_WARMUP", "2"))
time = Float(ENV.fetch("IPS_TIME", "5"))

Benchmark.ips do |x|
  x.config(warmup: warmup, time: time)

  candidates.each do |name, block|
    x.report(name, &block)
  end

  x.compare!
end

puts
puts "sizes:"

candidates.each do |name, block|
  output = block.call
  puts "#{name} q=#{quality}: #{output.bytesize}"
rescue StandardError => e
  puts "#{name} q=#{quality}: ERROR #{e.message.lines.first.to_s.strip}"
end
