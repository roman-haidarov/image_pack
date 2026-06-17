# frozen_string_literal: true

require_relative "lib/image_pack/version"
require_relative "ext/image_pack/mozjpeg_sources"

Gem::Specification.new do |spec|
  spec.name          = "image_pack"
  spec.version       = ImagePack::VERSION
  spec.authors       = ["Roman Haydarov"]
  spec.email         = ["romnhajdarov@gmail.com"]

  spec.summary       = "Ruby-native pure-C JPEG runtime: MozJPEG/libjpeg"
  spec.description   = "Single API, vendored pure-C MozJPEG/libjpeg codec, no tempfiles, " \
                       "Ruby 3.1+ native JPEG execution; Ruby 3.4+ enables Fiber::Scheduler-aware offload. " \
                       "Ships vendored C sources — no system libjpeg, git, or CMake required."
  spec.homepage      = "https://github.com/roman-haidarov/image_pack"
  spec.license       = "MIT"
  spec.required_ruby_version = ">= 3.1.0"

  spec.metadata["homepage_uri"]    = spec.homepage
  spec.metadata["source_code_uri"] = spec.homepage
  spec.metadata["changelog_uri"]   = "#{spec.homepage}/blob/main/CHANGELOG.md"

  mozjpeg_dir = "ext/image_pack/vendor/mozjpeg"
  mozjpeg_runtime_sources = ImagePackMozjpegSources::GEM_RUNTIME_C_SOURCES.map { |rel| File.join(mozjpeg_dir, rel) }
  mozjpeg_support_files = Dir[
    "#{mozjpeg_dir}/**/*.{h,h.in,inc,asm,S,in,txt,md}",
    "#{mozjpeg_dir}/simd/nasm/jsimdcfg.inc.h",
    "#{mozjpeg_dir}/win/*.inc"
  ]

  spec.files = (
    Dir["lib/**/*.rb"] +
    %w[
      ext/image_pack/extconf.rb
      ext/image_pack/image_pack.c
      ext/image_pack/mozjpeg_sources.rb
      ext/image_pack/vendor/.vendored
      README.md
      LICENSE.txt
      THIRD_PARTY_NOTICES.md
      CHANGELOG.md
    ] +
    mozjpeg_runtime_sources +
    mozjpeg_support_files
  ).select { |path| File.file?(path) }.uniq.sort

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
