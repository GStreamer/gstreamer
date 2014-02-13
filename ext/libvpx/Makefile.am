# Makefile.am - Rules for the built-in libvpx sources
#
# INTEL CONFIDENTIAL, FOR INTERNAL USE ONLY
# Copyright (C) 2014 Intel Corporation
#   Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
#
# @BEGIN_LICENSE@
# The source code contained or described herein and all documents
# related to the source code ("Material") are owned by Intel
# Corporation or its suppliers or licensors. Title to the Material
# remains with Intel Corporation or its suppliers and licensors. The
# Material contains trade secrets and proprietary and confidential
# information of Intel or its suppliers and licensors. The Material
# is protected by worldwide copyright and trade secret laws and
# treaty provisions. No part of the Material may be used, copied,
# reproduced, modified, published, uploaded, posted, transmitted,
# distributed, or disclosed in any way without Intel’s prior express
# written permission.
#
# No license under any patent, copyright, trade secret or other
# intellectual property right is granted to or conferred upon you by
# disclosure or delivery of the Materials, either expressly, by
# implication, inducement, estoppel or otherwise. Any license under
# such intellectual property rights must be express and approved by
# Intel in writing.
# @END_LICENSE@

lib_LTLIBRARIES =
if USE_LOCAL_CODEC_PARSERS_VP8
lib_LTLIBRARIES += libgstcodecparsers_vpx.la
endif

gst_vpx_source_c = gstlibvpx.c
gst_vpx_source_h = gstlibvpx.h

vpx_upstream	= upstream
vpx_srcdir	= $(srcdir)/$(vpx_upstream)
vpx_builddir	= $(builddir)/$(vpx_upstream)
vpx_versions	= libgstcodecparsers_vpx.vers

vpx_cflags = \
	-I$(vpx_srcdir) \
	-I$(vpx_builddir) \
	$(NULL)

vpx_libs = \
	-Wl,-Bsymbolic \
	-Wl,--whole-archive \
	-Wl,$(vpx_builddir)/libvpx.a \
	-Wl,--no-whole-archive \
	-Wl,--version-script,$(srcdir)/$(vpx_versions) \
	-Wl,-lpthread \
	-Wl,-lm \
	$(NULL)

libgstcodecparsers_vpx_la_SOURCES		= $(gst_vpx_source_c)
libgstcodecparsers_vpx_la_CFLAGS		= $(vpx_cflags)
libgstcodecparsers_vpx_la_DEPENDENCIES		= vpx.build
libgstcodecparsers_vpx_la_LINK			= $(LINK) $(vpx_libs)
EXTRA_libgstcodecparsers_vpx_la_DEPENDENCIES	= $(srcdir)/$(vpx_versions)

VPX_DEBUG	= yes

# Codecs
VP8_DECODER	= enable
VP8_ENCODER	= disable
VP9_DECODER	= disable
VP9_ENCODER	= disable

clean-local: vpx.clean
maintainer-clean-local: vpx.maintainer.clean

vpx.build: vpx.configure.stamp
	@$(MAKE) -C $(vpx_builddir)

vpx.clean:
	@[ -d $(vpx_builddir) ] && \
	$(MAKE) -C $(vpx_builddir) clean || :
	rm -f vpx.build.stamp vpx.configure.stamp

vpx.maintainer.clean: vpx.clean
	rm -rf $(vpx_builddir)

vpx.configure.stamp:
	@[ -d $(vpx_builddir) ] || mkdir $(vpx_builddir);		 \
	cd $(vpx_builddir)						;  \
	test "$(VPX_DEBUG)" = "yes"					&& \
	CONFIGURE_FLAGS="$$CONFIGURE_FLAGS --enable-debug"		; \
	$(abs_srcdir)/$(vpx_upstream)/configure $$CONFIGURE_FLAGS	\
		--enable-static						\
		--enable-pic						\
		--disable-shared					\
		--$(VP8_DECODER)-vp8-decoder				\
		--$(VP8_ENCODER)-vp8-encoder				\
		--$(VP9_DECODER)-vp9-decoder				\
		--$(VP9_ENCODER)-vp9-encoder				\
		--enable-runtime-cpu-detect				\
		--disable-md5						\
		--disable-examples					\
		--disable-docs						\
		--disable-unit-tests					&& \
	cd ..								&& \
	touch $@

CLEANFILES = vpx.build.stamp

# Files for packaging
include $(srcdir)/sources.frag

vpx_sources = \
	$(vpx_srcdir)/AUTHORS \
	$(vpx_srcdir)/CHANGELOG \
	$(vpx_srcdir)/LICENSE \
	$(vpx_srcdir)/PATENTS \
	$(vpx_srcdir)/build/make/Makefile \
	$(vpx_srcdir)/build/make/ads2gas.pl \
	$(vpx_srcdir)/build/make/ads2gas_apple.pl \
	$(vpx_srcdir)/build/make/configure.sh \
	$(vpx_srcdir)/build/make/gen_asm_deps.sh \
	$(vpx_srcdir)/build/make/obj_int_extract.c \
	$(vpx_srcdir)/build/make/rtcd.sh \
	$(vpx_srcdir)/build/make/version.sh \
	$(vpx_srcdir)/configure \
	$(vpx_srcdir)/vp8/common/rtcd_defs.sh \
	$(vpx_srcdir)/vp9/common/vp9_rtcd_defs.sh \
	$(vpx_srcdir)/vpx_scale/vpx_scale_rtcd.sh \
	$(vpx_source_mak:%.mk=$(vpx_srcdir)/%.mk) \
	$(vpx_source_c:%.c=$(vpx_srcdir)/%.c) \
	$(vpx_source_h:%.h=$(vpx_srcdir)/%.h) \
	$(vpx_source_asm:%.asm=$(vpx_srcdir)/%.asm) \
	$(NULL)

EXTRA_DIST = \
	sources.frag		\
	$(gst_vpx_source_h)	\
	$(vpx_sources)		\
	$(vpx_versions)		\
	$(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in vpx.configure.stamp
