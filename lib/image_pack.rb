# frozen_string_literal: true

required = Gem::Version.new("2.7.1")
current  = Gem::Version.new(RUBY_VERSION)

if current < required
  raise LoadError,
        "image_pack requires Ruby >= 2.7.1, got #{RUBY_VERSION}. " \
        "Ruby >= 3.4.0 is required only for execution: :offload."
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
  SUBSAMPLING_MODES = %i[auto 420 422 444].freeze
  TUNE_MODES = %i[hvs ssim ms_ssim psnr].freeze
  EFFORT_MODES = %i[default fast max].freeze
  SUBSAMPLING_TO_NATIVE = { auto: 0, "420": 1, "422": 2, "444": 3 }.freeze
  TUNE_TO_NATIVE = { hvs: 0, ssim: 1, ms_ssim: 2, psnr: 3 }.freeze
  EFFORT_TO_NATIVE = { default: 0, fast: 1, max: 2 }.freeze
  DEFAULT_QUALITY = 82
  DEFAULT_ALGO = :mozjpeg
  AUTO_444_QUALITY = 90

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
        simd: defined?(NATIVE_SIMD) ? NATIVE_SIMD : nil,
        offload_safe: offload_safe?
      }
    end

    def offload_safe?
      defined?(NATIVE_OFFLOAD_SAFE) && NATIVE_OFFLOAD_SAFE == true
    end

    def warn_if_scalar!
      return if !defined?(NATIVE_SIMD) || NATIVE_SIMD
      return if @scalar_warned

      @scalar_warned = true
      warn "ImagePack: this native build is scalar (no SIMD). On x86_64 install nasm " \
           "and reinstall the gem for ~3-4x faster JPEG encode/decode."
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
                      strip_icc: false,
                      trim: false,
                      execution: nil,
                      cancellable: false,
                      strict: false)
      warn_if_scalar!
      validate_boolean!(:progressive, progressive)
      validate_boolean!(:strip_metadata, strip_metadata)
      validate_boolean!(:strip_icc, strip_icc)
      validate_boolean!(:trim, trim)
      validate_boolean!(:cancellable, cancellable)
      validate_boolean!(:strict, strict)
      execution ||= configuration.execution
      validate_execution!(execution)
      validate_execution_supported!(execution)
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
                      has_scheduler ? 1 : 0,
                      strict ? 1 : 0,
                      strip_icc ? 1 : 0,
                      trim ? 1 : 0)
    end

    def compress(input,
                 output: nil,
                 algo: DEFAULT_ALGO,
                 quality: nil,
                 min_ssim: nil,
                 mozjpeg_trellis: true,
                 mozjpeg_scan_opt: nil,
                 progressive: nil,
                 strip_metadata: true,
                 strip_icc: false,
                 subsampling: :auto,
                 tune: :hvs,
                 effort: :default,
                 scale: 1,
                 execution: nil,
                 cancellable: false,
                 report: false,
                 strict: false)
      warn_if_scalar!
      validate_algo!(algo)
      validate_min_ssim!(min_ssim)
      validate_boolean!(:mozjpeg_trellis, mozjpeg_trellis)
      scan_opt_given = !mozjpeg_scan_opt.nil?
      mozjpeg_scan_opt = true unless scan_opt_given
      validate_boolean!(:mozjpeg_scan_opt, mozjpeg_scan_opt)
      progressive = default_progressive_for(algo, progressive)
      validate_boolean!(:progressive, progressive)
      validate_boolean!(:strip_metadata, strip_metadata)
      validate_boolean!(:strip_icc, strip_icc)
      validate_boolean!(:cancellable, cancellable)
      validate_boolean!(:report, report)
      validate_boolean!(:strict, strict)
      subsampling = validate_subsampling!(subsampling)
      tune = validate_tune!(tune)
      effort = validate_effort!(effort)
      scale_num, scale_denom = validate_scale!(scale)
      mozjpeg_scan_opt = false if effort == :fast && !scan_opt_given
      quality_was_given = !quality.nil?
      effective_quality = quality_was_given ? quality : DEFAULT_QUALITY
      effective_quality = 1 if min_ssim && !quality_was_given
      validate_quality!(effective_quality)
      execution ||= configuration.execution
      validate_execution!(execution)
      validate_execution_supported!(execution)
      validate_cancellable!(algo, execution, cancellable)

      normalized_input_kind = input_kind!(input)
      normalized_output_kind = output_kind!(output)
      has_scheduler = fiber_scheduler_active?

      __compress_jpeg(input, normalized_input_kind,
                      output, normalized_output_kind,
                      ALGO_TO_NATIVE.fetch(algo), effective_quality,
                      min_ssim ? min_ssim.to_f : 0.0,
                      mozjpeg_trellis ? 1 : 0,
                      progressive ? 1 : 0,
                      strip_metadata ? 1 : 0,
                      execution,
                      cancellable ? 1 : 0,
                      has_scheduler ? 1 : 0,
                      report ? 1 : 0,
                      strict ? 1 : 0,
                      mozjpeg_scan_opt ? 1 : 0,
                      strip_icc ? 1 : 0,
                      SUBSAMPLING_TO_NATIVE.fetch(subsampling),
                      TUNE_TO_NATIVE.fetch(tune),
                      EFFORT_TO_NATIVE.fetch(effort),
                      scale_num, scale_denom)
    end

    def compress_pixels(buffer,
                        width:,
                        height:,
                        channels:,
                        output: nil,
                        algo: DEFAULT_ALGO,
                        quality: nil,
                        min_ssim: nil,
                        mozjpeg_trellis: true,
                        mozjpeg_scan_opt: nil,
                        progressive: nil,
                        drop_alpha: nil,
                        exact_size: false,
                        subsampling: :auto,
                        tune: :hvs,
                        effort: :default,
                        execution: nil,
                        cancellable: false,
                        report: false,
                        strict: false)
      warn_if_scalar!
      validate_pixel_buffer!(buffer)
      validate_algo!(algo)
      validate_min_ssim!(min_ssim)
      validate_boolean!(:mozjpeg_trellis, mozjpeg_trellis)
      scan_opt_given = !mozjpeg_scan_opt.nil?
      mozjpeg_scan_opt = true unless scan_opt_given
      validate_boolean!(:mozjpeg_scan_opt, mozjpeg_scan_opt)
      progressive = default_progressive_for(algo, progressive)
      validate_boolean!(:progressive, progressive)
      validate_drop_alpha!(drop_alpha)
      validate_boolean!(:exact_size, exact_size)
      validate_boolean!(:cancellable, cancellable)
      validate_boolean!(:report, report)
      validate_boolean!(:strict, strict)
      subsampling = validate_subsampling!(subsampling)
      tune = validate_tune!(tune)
      effort = validate_effort!(effort)
      mozjpeg_scan_opt = false if effort == :fast && !scan_opt_given
      quality_was_given = !quality.nil?
      effective_quality = quality_was_given ? quality : DEFAULT_QUALITY
      effective_quality = 1 if min_ssim && !quality_was_given
      validate_quality!(effective_quality)
      validate_dimensions!(width, height, channels)
      execution ||= configuration.execution
      validate_execution!(execution)
      validate_execution_supported!(execution)
      validate_cancellable!(algo, execution, cancellable)

      if channels == 4
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

      if min_ssim && channels == 4
        raise UnsupportedError, "min_ssim is not supported for RGBA input"
      end

      __compress_pixels(buffer,
                        width, height, channels,
                        output, normalized_output_kind,
                        ALGO_TO_NATIVE.fetch(algo), effective_quality,
                        min_ssim ? min_ssim.to_f : 0.0,
                        mozjpeg_trellis ? 1 : 0,
                        progressive ? 1 : 0,
                        exact_size ? 1 : 0,
                        execution,
                        cancellable ? 1 : 0,
                        has_scheduler ? 1 : 0,
                        report ? 1 : 0,
                        strict ? 1 : 0,
                        mozjpeg_scan_opt ? 1 : 0,
                        SUBSAMPLING_TO_NATIVE.fetch(subsampling),
                        TUNE_TO_NATIVE.fetch(tune),
                        EFFORT_TO_NATIVE.fetch(effort))
    end

    def inspect_image(input)
      warn_if_scalar!
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

      raise InvalidArgumentError, "output must be nil, String path, or Pathname"
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

    def validate_subsampling!(value)
      value = value.to_s.to_sym if value.is_a?(Integer) || value.is_a?(String)
      return value if SUBSAMPLING_MODES.include?(value)

      raise InvalidArgumentError,
            "subsampling must be one of #{SUBSAMPLING_MODES}, got: #{value.inspect}"
    end

    def validate_tune!(value)
      return value if TUNE_MODES.include?(value)

      raise InvalidArgumentError, "tune must be one of #{TUNE_MODES}, got: #{value.inspect}"
    end

    def validate_effort!(value)
      return value if EFFORT_MODES.include?(value)

      raise InvalidArgumentError, "effort must be one of #{EFFORT_MODES}, got: #{value.inspect}"
    end

    def validate_scale!(value)
      num, denom =
        case value
        when Integer
          raise InvalidArgumentError, "scale must be a positive fraction, got: #{value.inspect}" unless value.positive?

          [value, 1]
        when Float
          if value == 1.0
            [1, 1]
          elsif value == 0.5
            [1, 2]
          elsif value == 0.25
            [1, 4]
          elsif value == 0.125
            [1, 8]
          else
            raise InvalidArgumentError,
                  "scale float must be 1.0, 0.5, 0.25, or 0.125, got: #{value.inspect}"
          end
        when Rational
          [value.numerator, value.denominator]
        when Array
          raise InvalidArgumentError, "scale array must be [num, denom], got: #{value.inspect}" unless value.length == 2

          value
        else
          raise InvalidArgumentError,
                "scale must be 1, 0.5, 0.25, 0.125, Rational, or [num, denom], got: #{value.inspect}"
        end

      unless num.is_a?(Integer) && denom.is_a?(Integer) && num.positive? && denom.positive?
        raise InvalidArgumentError, "scale numerator/denominator must be Integers > 0, got: #{value.inspect}"
      end

      [num, denom]
    end

    def validate_boolean!(name, value)
      return if value == true || value == false

      raise InvalidArgumentError, "#{name} must be true or false, got: #{value.inspect}"
    end


    def default_progressive_for(algo, value)
      return value unless value.nil?

      ALGO_TO_NATIVE.fetch(algo) == :mozjpeg
    end

    def validate_drop_alpha!(value)
      return if value.nil? || value == true || value == false

      raise InvalidArgumentError, "drop_alpha must be true, false, or nil, got: #{value.inspect}"
    end

    def validate_execution!(execution)
      return if EXECUTION_MODES.include?(execution)

      raise InvalidArgumentError, "execution must be one of #{EXECUTION_MODES}, got: #{execution.inspect}"
    end

    def validate_execution_supported!(execution)
      return unless execution == :offload
      return if offload_safe?

      raise UnsupportedError,
            "execution: :offload is unavailable in this runtime; it requires Ruby >= 3.4.0 and IMAGE_PACK_DISABLE_OFFLOAD must not be set"
    end

    def validate_dimensions!(width, height, channels)
      raise InvalidArgumentError, "width must be Integer > 0" unless width.is_a?(Integer) && width.positive?
      raise InvalidArgumentError, "height must be Integer > 0" unless height.is_a?(Integer) && height.positive?
      raise InvalidArgumentError, "channels must be 1, 3 or 4" unless [1, 3, 4].include?(channels)
    end

    def validate_pixel_buffer!(buffer)
      return if buffer.is_a?(String)
      return if defined?(IO::Buffer) && buffer.is_a?(IO::Buffer)

      raise InvalidArgumentError, "pixel buffer must be a String or IO::Buffer"
    end

    def validate_cancellable!(_algo, execution, cancellable)
      return unless cancellable
      return unless execution == :direct

      raise InvalidArgumentError,
            "cancellable: true requires execution: :nogvl, :offload, or :auto"
    end

    def fiber_scheduler_active?
      offload_safe? && Fiber.respond_to?(:scheduler) && !Fiber.scheduler.nil?
    end
  end
end
