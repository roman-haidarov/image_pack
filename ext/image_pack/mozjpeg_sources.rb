# frozen_string_literal: true

# Single source of truth for the vendored MozJPEG runtime file set.
#
# Keep this file free of build-time side effects: it is required by
# image_pack.gemspec, ext/image_pack/extconf.rb, script/vendor_libs.rb, and
# packaging tests. Files listed as include templates are copied/packaged because
# MozJPEG includes them textually from other C/ASM files, but they must not be
# compiled as standalone translation units.
module ImagePackMozjpegSources
  VERSION = "4.1.5"
  VERSION_NUMBER = "4001005"
  SHA256 = "9fcbb7171f6ac383f5b391175d6fb3acde5e64c4c4727274eade84ed0998fcc1"

  LICENSE_AND_DOC_FILES = %w[
    LICENSE.md
    README.md
    README-mozilla.txt
    README-turbo.txt
    README.ijg
  ].freeze

  # Files compiled as standalone translation units in the scalar/runtime core.
  TOPLEVEL_C_SOURCES = %w[
    jcapimin.c
    jcapistd.c
    jccoefct.c
    jccolor.c
    jcdctmgr.c
    jchuff.c
    jcext.c
    jcicc.c
    jcinit.c
    jcmainct.c
    jcmarker.c
    jcmaster.c
    jcomapi.c
    jcparam.c
    jcphuff.c
    jcprepct.c
    jcsample.c
    jctrans.c
    jdapimin.c
    jdapistd.c
    jdatadst.c
    jdatasrc.c
    jdcoefct.c
    jdcolor.c
    jddctmgr.c
    jdhuff.c
    jdicc.c
    jdinput.c
    jdmainct.c
    jdmarker.c
    jdmaster.c
    jdmerge.c
    jdphuff.c
    jdpostct.c
    jdsample.c
    jdtrans.c
    jerror.c
    jfdctflt.c
    jfdctfst.c
    jfdctint.c
    jidctflt.c
    jidctfst.c
    jidctint.c
    jidctred.c
    jquant1.c
    jquant2.c
    jutils.c
    jmemmgr.c
    jmemnobs.c
    transupp.c
  ].freeze

  # MozJPEG/libjpeg-turbo uses these top-level .c files as textual include
  # templates. They are required at compile time but must not be added to $srcs.
  TOPLEVEL_INCLUDE_TEMPLATES = %w[
    jccolext.c
    jdcol565.c
    jdcolext.c
    jdmrg565.c
    jdmrgext.c
    jstdhuff.c
  ].freeze

  TOPLEVEL_RUNTIME_C_FILES = (TOPLEVEL_C_SOURCES + TOPLEVEL_INCLUDE_TEMPLATES + %w[jsimd_none.c]).freeze

  NEON_AARCH64_C_SOURCES = %w[
    simd/arm/aarch64/jsimd.c
    simd/arm/aarch64/jchuff-neon.c
    simd/arm/jccolor-neon.c
    simd/arm/jcgray-neon.c
    simd/arm/jcphuff-neon.c
    simd/arm/jcsample-neon.c
    simd/arm/jdcolor-neon.c
    simd/arm/jdmerge-neon.c
    simd/arm/jdsample-neon.c
    simd/arm/jfdctfst-neon.c
    simd/arm/jfdctint-neon.c
    simd/arm/jidctfst-neon.c
    simd/arm/jidctint-neon.c
    simd/arm/jidctred-neon.c
    simd/arm/jquanti-neon.c
  ].freeze

  # AArch64 NEON C include templates required by the NEON sources above.
  NEON_AARCH64_INCLUDE_TEMPLATES = %w[
    simd/arm/aarch64/jccolext-neon.c
    simd/arm/jcgryext-neon.c
    simd/arm/jdcolext-neon.c
    simd/arm/jdmrgext-neon.c
  ].freeze

  X86_64_C_SOURCES = %w[
    simd/x86_64/jsimd.c
  ].freeze

  X86_64_ASM_SOURCES = %w[
    simd/x86_64/jsimdcpu.asm
    simd/x86_64/jfdctflt-sse.asm
    simd/x86_64/jccolor-sse2.asm
    simd/x86_64/jcgray-sse2.asm
    simd/x86_64/jchuff-sse2.asm
    simd/x86_64/jcphuff-sse2.asm
    simd/x86_64/jcsample-sse2.asm
    simd/x86_64/jdcolor-sse2.asm
    simd/x86_64/jdmerge-sse2.asm
    simd/x86_64/jdsample-sse2.asm
    simd/x86_64/jfdctfst-sse2.asm
    simd/x86_64/jfdctint-sse2.asm
    simd/x86_64/jidctflt-sse2.asm
    simd/x86_64/jidctfst-sse2.asm
    simd/x86_64/jidctint-sse2.asm
    simd/x86_64/jidctred-sse2.asm
    simd/x86_64/jquantf-sse2.asm
    simd/x86_64/jquanti-sse2.asm
    simd/x86_64/jccolor-avx2.asm
    simd/x86_64/jcgray-avx2.asm
    simd/x86_64/jcsample-avx2.asm
    simd/x86_64/jdcolor-avx2.asm
    simd/x86_64/jdmerge-avx2.asm
    simd/x86_64/jdsample-avx2.asm
    simd/x86_64/jfdctint-avx2.asm
    simd/x86_64/jidctint-avx2.asm
    simd/x86_64/jquanti-avx2.asm
  ].freeze

  # .c files that must be shipped in the gem. This includes compile units and
  # textual include templates; extconf decides which compile units to add to $srcs.
  GEM_RUNTIME_C_SOURCES = (
    TOPLEVEL_RUNTIME_C_FILES +
    NEON_AARCH64_C_SOURCES +
    NEON_AARCH64_INCLUDE_TEMPLATES +
    X86_64_C_SOURCES
  ).freeze

  TOPLEVEL_EXTENSIONS = %w[.h .in .txt .md].freeze

  # `.in` matters under simd/ too: simd/arm/neon-compat.h.in is the
  # CMake-generated header template processed by extconf.rb at build time.
  SIMD_EXTENSIONS = %w[.c .h .asm .inc .S .in].freeze

  def self.mozjpeg_file?(relative_path)
    return false if relative_path.empty?

    if relative_path.include?("/")
      return false unless relative_path.start_with?("simd/")
      return false if relative_path.start_with?("simd/i386/", "simd/mips/", "simd/mips64/",
                                                "simd/powerpc/", "simd/arm/aarch32/")
      return false unless relative_path.start_with?("simd/arm/", "simd/x86_64/", "simd/nasm/") ||
                          relative_path == "simd/jsimd.h"

      return SIMD_EXTENSIONS.include?(File.extname(relative_path))
    end

    basename = File.basename(relative_path)
    LICENSE_AND_DOC_FILES.include?(basename) ||
      TOPLEVEL_RUNTIME_C_FILES.include?(basename) ||
      TOPLEVEL_EXTENSIONS.include?(File.extname(basename))
  end
end
