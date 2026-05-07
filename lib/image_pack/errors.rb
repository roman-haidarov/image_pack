# frozen_string_literal: true

module ImagePack
  class Error < StandardError; end
  class InvalidArgumentError < Error; end
  class InvalidImageError < Error; end
  class UnsupportedError < Error; end
  class LimitExceededError < Error; end
  class EncodeError < Error; end
  class QualityConstraintError < Error; end
  class OutOfMemoryError < Error; end
  class CancelledError < Error; end
end
