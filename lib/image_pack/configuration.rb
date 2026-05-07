# frozen_string_literal: true

module ImagePack
  class Configuration
    attr_accessor :execution,
                  :direct_input_threshold,
                  :direct_pixel_threshold,
                  :max_pixels,
                  :max_width,
                  :max_height,
                  :max_output_size

    def initialize
      @execution              = :auto
      @direct_input_threshold = 128 * 1024
      @direct_pixel_threshold = 1 * 1024 * 1024
      @max_pixels             = 100_000_000
      @max_width              = 30_000
      @max_height             = 30_000
      @max_output_size        = 256 * 1024 * 1024
    end
  end
end
