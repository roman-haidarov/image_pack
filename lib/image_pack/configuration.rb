# frozen_string_literal: true

module ImagePack
  class Configuration
    SIZE_ATTRIBUTES = %i[
      direct_input_threshold
      direct_pixel_threshold
      max_pixels
      max_output_size
      max_input_size
    ].freeze

    INT_ATTRIBUTES = %i[
      max_width
      max_height
    ].freeze

    attr_reader :execution,
                :fallback_engine,
                :direct_input_threshold,
                :direct_pixel_threshold,
                :max_pixels,
                :max_width,
                :max_height,
                :max_output_size,
                :max_input_size

    def initialize
      @execution              = :auto
      @fallback_engine        = :mozjpeg
      @direct_input_threshold = 128 * 1024
      @direct_pixel_threshold = 1 * 1024 * 1024
      @max_pixels             = 100_000_000
      @max_width              = 30_000
      @max_height             = 30_000
      @max_output_size        = 256 * 1024 * 1024
      @max_input_size         = 256 * 1024 * 1024
    end

    def execution=(value)
      unless ImagePack::EXECUTION_MODES.include?(value)
        raise InvalidArgumentError, "execution must be one of #{ImagePack::EXECUTION_MODES}, got: #{value.inspect}"
      end

      if value == :offload && ImagePack.respond_to?(:offload_safe?) && !ImagePack.offload_safe?
        raise UnsupportedError,
              "execution: :offload is unavailable in this runtime; it requires Ruby >= 3.4.0 and IMAGE_PACK_DISABLE_OFFLOAD must not be set"
      end

      @execution = value
    end

    def fallback_engine=(value)
      allowed = %i[turbo mozjpeg]
      unless allowed.include?(value)
        raise InvalidArgumentError, "fallback_engine must be one of #{allowed}, got: #{value.inspect}"
      end

      @fallback_engine = value
    end

    SIZE_ATTRIBUTES.each do |name|
      define_method("#{name}=") do |value|
        instance_variable_set("@#{name}", validate_nonnegative_integer!(name, value))
      end
    end

    INT_ATTRIBUTES.each do |name|
      define_method("#{name}=") do |value|
        instance_variable_set("@#{name}", validate_nonnegative_integer!(name, value))
      end
    end

    private

    def validate_nonnegative_integer!(name, value)
      return value if value.is_a?(Integer) && value >= 0

      raise InvalidArgumentError, "#{name} must be an Integer >= 0, got: #{value.inspect}"
    end
  end
end
