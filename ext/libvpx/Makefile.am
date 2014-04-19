# Makefile.am - Rules for the built-in libvpx sources
#
# Copyright (C) 2014 Intel Corporation
#   Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

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

$(gst_vpx_source_c): vpx.build

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
	$(NULL)

EXTRA_DIST = \
	sources.frag		\
	$(gst_vpx_source_h)	\
	$(vpx_sources)		\
	$(vpx_versions)		\
	$(NULL)

# Avoid implicit rule that tries to compile .asm.o to .asm
dist-hook:
	for f in $(vpx_source_asm); do					\
	  mkdir -p $(distdir)/$(vpx_upstream)/$$(dirname $$f);		\
	  cp -fpR $(vpx_srcdir)/$$f $(distdir)/$(vpx_upstream)/$$f;	\
	done

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in vpx.configure.stamp
