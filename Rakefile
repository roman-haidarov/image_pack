# frozen_string_literal: true

require "bundler/gem_tasks"
require "rake/testtask"
require "rake/extensiontask"

Rake::ExtensionTask.new("image_pack") do |ext|
  ext.lib_dir = "lib/image_pack"
  ext.ext_dir = "ext/image_pack"
end

Rake::TestTask.new(:test) do |t|
  t.libs << "test"
  t.libs << "lib"
  t.test_files = FileList["test/**/*_test.rb", "test/**/test_*.rb"]
end

desc "Download and pin vendored native libraries"
task :vendor do
  ruby "script/vendor_libs.rb"
end

desc "Vendor native libs, compile extension, and run tests"
task full_build: %i[vendor compile test]

task default: %i[compile test]

task console: :compile do
  require "irb"
  require_relative "lib/image_pack"
  ARGV.clear
  IRB.start
end
