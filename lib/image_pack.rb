# frozen_string_literal: true

required = Gem::Version.new("3.4.0")
current  = Gem::Version.new(RUBY_VERSION)

if current < required
  raise LoadError,
        "image_pack requires Ruby >= 3.4.0, got #{RUBY_VERSION}. " \
        "Reason: gem relies on RB_NOGVL_OFFLOAD_SAFE / Fiber::Scheduler hooks."
end

require "pathname"
require_relative "image_pack/version"
require_relative "image_pack/errors"
require_relative "image_pack/configuration"

begin
  require "image_pack/image_pack"
rescue LoadError
  ext_dir = File.expand_path("image_pack", __dir__)
  so_path = %w[.bundle .so .dll]
    .map { |ext| File.join(ext_dir, "image_pack#{ext}") }
    .find { |path| File.file?(path) }

  raise LoadError, "Could not find compiled ImagePack extension. Run: bundle exec rake compile" unless so_path

  require so_path
end

module ImagePack
  ALGOS = %i[jpeg_turbo mozjpeg fast size].freeze
  ALGO_TO_NATIVE = { jpeg_turbo: :jpeg_turbo, mozjpeg: :mozjpeg, fast: :jpeg_turbo, size: :mozjpeg }.freeze
  EXECUTION_MODES = %i[direct nogvl offload auto].freeze
  DEFAULT_QUALITY = 82
  DEFAULT_ALGO = :mozjpeg

  class << self
    def configuration
      @configuration ||= Configuration.new
    end

    def configure
      return configuration unless block_given?

      yield(configuration)
      configuration
    end

    def build_info
      {
        version: VERSION,
        mozjpeg: defined?(NATIVE_MOZJPEG_VERSION) ? NATIVE_MOZJPEG_VERSION : nil,
        simd: defined?(NATIVE_SIMD) ? NATIVE_SIMD : nil
      }
    end

    def compress_bytes(bytes, **options)
      raise InvalidArgumentError, "bytes must be a String" unless bytes.is_a?(String)

      compress(bytes.b, **options)
    end

    def compress_file(path, **options)
      pathname = Pathname(path)
      raise InvalidArgumentError, "input path does not exist: #{pathname}" unless pathname.file?

      compress(pathname, **options)
    end

    def optimize_bytes(bytes, **options)
      raise InvalidArgumentError, "bytes must be a String" unless bytes.is_a?(String)

      optimize_jpeg(bytes.b, **options)
    end

    def optimize_file(path, **options)
      pathname = Pathname(path)
      raise InvalidArgumentError, "input path does not exist: #{pathname}" unless pathname.file?

      optimize_jpeg(pathname, **options)
    end

    def optimize_jpeg(input,
                      output: nil,
                      progressive: true,
                      strip_metadata: false,
                      execution: nil,
                      cancellable: false)
      execution ||= configuration.execution
      validate_execution!(execution)
      validate_cancellable!(:lossless_optimize, execution, cancellable)

      normalized_input_kind = input_kind!(input)
      normalized_output_kind = output_kind!(output)
      has_scheduler = fiber_scheduler_active?

      __optimize_jpeg(input, normalized_input_kind,
                      output, normalized_output_kind,
                      progressive ? 1 : 0,
                      strip_metadata ? 1 : 0,
                      execution,
                      cancellable ? 1 : 0,
                      has_scheduler ? 1 : 0)
    end

    def compress(input,
                 output: nil,
                 algo: DEFAULT_ALGO,
                 quality: nil,
                 min_ssim: nil,
                 mozjpeg_trellis: true,
                 progressive: false,
                 strip_metadata: true,
                 execution: nil,
                 cancellable: false)
      validate_algo!(algo)
      validate_min_ssim!(min_ssim)
      validate_mozjpeg_trellis!(mozjpeg_trellis)
      quality_was_given = !quality.nil?
      effective_quality = quality_was_given ? quality : DEFAULT_QUALITY
      effective_quality = 1 if min_ssim && !quality_was_given
      validate_quality!(effective_quality)
      execution ||= configuration.execution
      validate_execution!(execution)
      validate_cancellable!(algo, execution, cancellable)

      normalized_input_kind = input_kind!(input)
      normalized_output_kind = output_kind!(output)
      has_scheduler = fiber_scheduler_active?

      __compress_jpeg(input, normalized_input_kind,
                      output, normalized_output_kind,
                      ALGO_TO_NATIVE.fetch(algo), effective_quality.to_i,
                      min_ssim ? min_ssim.to_f : 0.0,
                      mozjpeg_trellis ? 1 : 0,
                      progressive ? 1 : 0,
                      strip_metadata ? 1 : 0,
                      execution,
                      cancellable ? 1 : 0,
                      has_scheduler ? 1 : 0)
    end

    def compress_pixels(buffer,
                        width:,
                        height:,
                        channels:,
                        output: nil,
                        algo: DEFAULT_ALGO,
                        quality: DEFAULT_QUALITY,
                        min_ssim: nil,
                        progressive: false,
                        drop_alpha: nil,
                        execution: nil,
                        cancellable: false)
      validate_algo!(algo)
      validate_min_ssim!(min_ssim)
      validate_quality!(quality)
      validate_dimensions!(width, height, channels)
      execution ||= configuration.execution
      validate_execution!(execution)
      validate_cancellable!(algo, execution, cancellable)

      if channels.to_i == 4
        case drop_alpha
        when nil
          warn "ImagePack.compress_pixels: RGBA input has its alpha channel " \
               "discarded (JPEG cannot store alpha). Pass drop_alpha: true to " \
               "silence this warning, or drop_alpha: false to raise instead."
        when false
          raise UnsupportedError,
                "JPEG cannot store an alpha channel. Pass drop_alpha: true to drop it explicitly."
        end
      end

      normalized_output_kind = output_kind!(output)
      has_scheduler = fiber_scheduler_active?

      if min_ssim && channels.to_i == 4
        raise UnsupportedError, "min_ssim is not supported for RGBA input"
      end

      __compress_pixels(buffer,
                        width.to_i, height.to_i, channels.to_i,
                        output, normalized_output_kind,
                        ALGO_TO_NATIVE.fetch(algo), quality.to_i,
                        min_ssim ? min_ssim.to_f : 0.0,
                        progressive ? 1 : 0,
                        execution,
                        cancellable ? 1 : 0,
                        has_scheduler ? 1 : 0)
    end

    def inspect_image(input)
      __inspect_image(input, input_kind!(input))
    end

    private

    def input_kind!(input)
      case input
      when String
        if input.encoding == Encoding::BINARY || input.encoding == Encoding::ASCII_8BIT
          :bytes
        elsif input.bytesize < 4096 && !input.include?("\0") && File.file?(input)
          :path
        else
          :bytes
        end
      when Pathname
        raise InvalidArgumentError, "input path does not exist: #{input}" unless input.file?

        :path
      else
        if defined?(IO::Buffer) && input.is_a?(IO::Buffer)
          :io_buffer
        else
          raise InvalidArgumentError,
                "input must be binary String bytes, path String, Pathname, or IO::Buffer"
        end
      end
    end

    def output_kind!(output)
      return :return_string if output.nil?
      return :path if output.is_a?(String) || output.is_a?(Pathname)

      raise InvalidArgumentError, "output must be nil, String path, or Pathname in v0.2.2"
    end

    def validate_algo!(algo)
      return if ALGOS.include?(algo)

      raise InvalidArgumentError, "algo must be one of #{ALGOS}, got: #{algo.inspect}"
    end

    def validate_quality!(quality)
      return if quality.is_a?(Integer) && quality.between?(1, 100)

      raise InvalidArgumentError, "quality must be Integer 1..100, got: #{quality.inspect}"
    end

    def validate_min_ssim!(min_ssim)
      return if min_ssim.nil?
      return if min_ssim.is_a?(Numeric) && min_ssim.positive? && min_ssim <= 1.0

      raise InvalidArgumentError, "min_ssim must be Numeric > 0.0 and <= 1.0, got: #{min_ssim.inspect}"
    end


    def validate_mozjpeg_trellis!(value)
      return if value == true || value == false

      raise InvalidArgumentError, "mozjpeg_trellis must be true or false, got: #{value.inspect}"
    end

    def validate_execution!(execution)
      return if EXECUTION_MODES.include?(execution)

      raise InvalidArgumentError, "execution must be one of #{EXECUTION_MODES}, got: #{execution.inspect}"
    end

    def validate_dimensions!(width, height, channels)
      raise InvalidArgumentError, "width must be > 0" unless width.is_a?(Integer) && width.positive?
      raise InvalidArgumentError, "height must be > 0" unless height.is_a?(Integer) && height.positive?
      raise InvalidArgumentError, "channels must be 1, 3 or 4" unless [1, 3, 4].include?(channels)
    end

    def validate_cancellable!(_algo, execution, cancellable)
      return unless cancellable
      return unless execution == :direct

      raise InvalidArgumentError,
            "cancellable: true requires execution: :nogvl, :offload, or :auto"
    end

    def fiber_scheduler_active?
      Fiber.respond_to?(:scheduler) && !Fiber.scheduler.nil?
    end
  end
end
