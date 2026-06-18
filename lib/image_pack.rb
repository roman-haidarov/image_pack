# frozen_string_literal: true

required = Gem::Version.new("3.1.0")
current  = Gem::Version.new(RUBY_VERSION)

if current < required
  raise LoadError,
        "image_pack requires Ruby >= 3.1.0, got #{RUBY_VERSION}. " \
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
  ENGINES = %i[jpegli turbo mozjpeg].freeze
  ENGINE_TO_NATIVE = { jpegli: :jpegli, turbo: :jpeg_turbo, mozjpeg: :mozjpeg }.freeze
  EXECUTION_MODES = %i[direct nogvl offload auto].freeze
  DEFAULT_QUALITY = 82
  DEFAULT_ENGINE = :mozjpeg
  EXPERIMENTAL_ENGINES = %i[jpegli].freeze

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
        jpegli: {
          binary: jpegli_binary_path,
          available: jpegli_available?
        },
        engines: ENGINES,
        default_engine: DEFAULT_ENGINE,
        experimental_engines: EXPERIMENTAL_ENGINES,
        simd: defined?(NATIVE_SIMD) ? NATIVE_SIMD : nil,
        offload_safe: offload_safe?
      }
    end

    def offload_safe?
      defined?(NATIVE_OFFLOAD_SAFE) && NATIVE_OFFLOAD_SAFE == true
    end

    def jpegli_binary_path
      configured = ENV["IMAGE_PACK_JPEGLI_BIN"]
      if configured && !configured.empty?
        return configured if configured.include?(File::SEPARATOR) && File.file?(configured) && File.executable?(configured)
        return path_lookup(configured) unless configured.include?(File::SEPARATOR)

        return nil
      end

      candidates = [
        File.expand_path("../ext/image_pack/vendor/jpegli/bin/cjpegli", __dir__),
        File.expand_path("../ext/image_pack/vendor/jpegli/tools/cjpegli", __dir__),
        File.expand_path("../ext/image_pack/vendor/jpegli/cjpegli", __dir__)
      ]
      candidates.find { |path| File.file?(path) && File.executable?(path) } || path_lookup("cjpegli")
    end

    def jpegli_available?
      !!jpegli_binary_path
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
                      cancellable: false,
                      strict: false)
      validate_boolean!(:progressive, progressive)
      validate_boolean!(:strip_metadata, strip_metadata)
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
                      strict ? 1 : 0)
    end

    def compress(input,
                 output: nil,
                 engine: nil,
                 quality: nil,
                 min_ssim: nil,
                 mozjpeg_trellis: true,
                 progressive: false,
                 strip_metadata: true,
                 execution: nil,
                 cancellable: false,
                 report: false,
                 strict: false)
      engine = resolve_engine!(engine)
      validate_min_ssim!(min_ssim)
      validate_boolean!(:mozjpeg_trellis, mozjpeg_trellis)
      validate_boolean!(:progressive, progressive)
      validate_boolean!(:strip_metadata, strip_metadata)
      validate_boolean!(:cancellable, cancellable)
      validate_boolean!(:report, report)
      validate_boolean!(:strict, strict)
      jpegli_bin = jpegli_binary_for!(engine, min_ssim)
      quality_was_given = !quality.nil?
      effective_quality = quality_was_given ? quality : DEFAULT_QUALITY
      effective_quality = 1 if min_ssim && !quality_was_given
      validate_quality!(effective_quality)
      execution ||= configuration.execution
      validate_execution!(execution)
      validate_execution_supported!(execution)
      validate_cancellable!(engine, execution, cancellable)

      normalized_input_kind = input_kind!(input)
      normalized_output_kind = output_kind!(output)
      has_scheduler = fiber_scheduler_active?

      __compress_jpeg(input, normalized_input_kind,
                      output, normalized_output_kind,
                      ENGINE_TO_NATIVE.fetch(engine), effective_quality,
                      min_ssim ? min_ssim.to_f : 0.0,
                      mozjpeg_trellis ? 1 : 0,
                      progressive ? 1 : 0,
                      strip_metadata ? 1 : 0,
                      execution,
                      cancellable ? 1 : 0,
                      has_scheduler ? 1 : 0,
                      report ? 1 : 0,
                      strict ? 1 : 0,
                      jpegli_bin)
    end

    def compress_pixels(buffer,
                        width:,
                        height:,
                        channels:,
                        output: nil,
                        engine: nil,
                        quality: nil,
                        min_ssim: nil,
                        mozjpeg_trellis: true,
                        progressive: false,
                        drop_alpha: nil,
                        exact_size: false,
                        execution: nil,
                        cancellable: false,
                        report: false,
                        strict: false)
      validate_pixel_buffer!(buffer)
      engine = resolve_engine!(engine)
      validate_min_ssim!(min_ssim)
      validate_boolean!(:mozjpeg_trellis, mozjpeg_trellis)
      validate_boolean!(:progressive, progressive)
      validate_drop_alpha!(drop_alpha)
      validate_boolean!(:exact_size, exact_size)
      validate_boolean!(:cancellable, cancellable)
      validate_boolean!(:report, report)
      validate_boolean!(:strict, strict)
      jpegli_bin = jpegli_binary_for!(engine, min_ssim)
      quality_was_given = !quality.nil?
      effective_quality = quality_was_given ? quality : DEFAULT_QUALITY
      effective_quality = 1 if min_ssim && !quality_was_given
      validate_quality!(effective_quality)
      validate_dimensions!(width, height, channels)
      execution ||= configuration.execution
      validate_execution!(execution)
      validate_execution_supported!(execution)
      validate_cancellable!(engine, execution, cancellable)

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
                        ENGINE_TO_NATIVE.fetch(engine), effective_quality,
                        min_ssim ? min_ssim.to_f : 0.0,
                        mozjpeg_trellis ? 1 : 0,
                        progressive ? 1 : 0,
                        exact_size ? 1 : 0,
                        execution,
                        cancellable ? 1 : 0,
                        has_scheduler ? 1 : 0,
                        report ? 1 : 0,
                        strict ? 1 : 0,
                        jpegli_bin)
    end

    def inspect_image(input)
      __inspect_image(input, input_kind!(input))
    end

    private

    def path_lookup(command)
      ENV["PATH"].to_s.split(File::PATH_SEPARATOR).each do |dir|
        path = File.join(dir, command)
        return path if File.file?(path) && File.executable?(path)
      end
      nil
    end

    def resolve_engine!(engine)
      engine = DEFAULT_ENGINE if engine.nil?
      validate_engine!(engine)
      engine
    end

    def jpegli_binary_for!(engine, min_ssim)
      return nil unless engine == :jpegli

      if min_ssim
        raise UnsupportedError,
              "min_ssim is not supported with engine: :jpegli; use engine: :turbo or :mozjpeg"
      end

      path = jpegli_binary_path
      unless path
        raise UnsupportedError,
              "engine: :jpegli requires the cjpegli helper, which was not found. " \
              "Install it and set IMAGE_PACK_JPEGLI_BIN, or use engine: :turbo or :mozjpeg."
      end
      path
    end

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

    def validate_engine!(engine)
      return if ENGINES.include?(engine)

      raise InvalidArgumentError, "engine must be one of #{ENGINES}, got: #{engine.inspect}"
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

    def validate_boolean!(name, value)
      return if value == true || value == false

      raise InvalidArgumentError, "#{name} must be true or false, got: #{value.inspect}"
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

    def validate_cancellable!(engine, execution, cancellable)
      return unless cancellable

      if engine == :jpegli
        raise UnsupportedError, "cancellable: true is not supported with engine: :jpegli"
      end

      return unless execution == :direct

      raise InvalidArgumentError,
            "cancellable: true requires execution: :nogvl, :offload, or :auto"
    end

    def fiber_scheduler_active?
      offload_safe? && Fiber.respond_to?(:scheduler) && !Fiber.scheduler.nil?
    end
  end
end
