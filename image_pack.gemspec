# frozen_string_literal: true

require_relative "lib/image_pack/version"

Gem::Specification.new do |spec|
  spec.name          = "image_pack"
  spec.version       = ImagePack::VERSION
  spec.authors       = ["Roman Haydarov"]
  spec.email         = ["romnhajdarov@gmail.com"]

  spec.summary       = "Ruby-native pure-C JPEG runtime: MozJPEG/libjpeg"
  spec.description   = "Single API, vendored pure-C MozJPEG/libjpeg codec, no tempfiles, " \
                       "Ruby 3.4 Fiber::Scheduler-aware native execution. " \
                       "Ships vendored C sources — no system libjpeg, git, or CMake required."
  spec.homepage      = "https://github.com/roman-haidarov/image_pack"
  spec.license       = "MIT"
  spec.required_ruby_version = ">= 3.4.0"

  spec.metadata["homepage_uri"]    = spec.homepage
  spec.metadata["source_code_uri"] = spec.homepage
  spec.metadata["changelog_uri"]   = "#{spec.homepage}/blob/main/CHANGELOG.md"

  spec.files = Dir[
    "lib/**/*.rb",
    "ext/**/*.{c,h,rb,in,txt,md}",
    "ext/image_pack/vendor/**/*",
    "ext/image_pack/vendor/.vendored",
    "README.md",
    "LICENSE.txt",
    "THIRD_PARTY_NOTICES.md",
    "CHANGELOG.md"
  ]

  spec.bindir        = "exe"
  spec.executables   = []
  spec.require_paths = ["lib"]
  spec.extensions    = ["ext/image_pack/extconf.rb"]

  spec.add_development_dependency "rake", "~> 13.0"
  spec.add_development_dependency "rake-compiler", "~> 1.2"
  spec.add_development_dependency "minitest", "~> 5.0"
  spec.add_development_dependency "benchmark-ips", "~> 2.13"
  spec.add_development_dependency "async", "~> 2.21"
end
