DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

if BUILD_EXTERNAL
SUBDIRS_EXT = ext
else
SUBDIRS_EXT =
endif

SUBDIRS = 			\
	pkgconfig		\
	gst-libs 		\
	gst sys $(SUBDIRS_EXT) 	\
	tools 			\
	tests 			\
	docs			\
	po 			\
	common 			\
	m4

DIST_SUBDIRS = 			\
	pkgconfig		\
	docs			\
	gst-libs		\
	gst sys ext		\
	tools 			\
	tests 			\
	po 			\
	common 			\
	m4

EXTRA_DIST = \
	depcomp \
	AUTHORS COPYING COPYING.LIB NEWS README RELEASE REQUIREMENTS \
	ChangeLog gst-plugins-base.doap autogen.sh \
	$(shell find "$(top_srcdir)" -type f -name meson.build ! -path "$(top_srcdir)/$(PACKAGE_TARNAME)-*" ) \
	gst-libs/gst/audio/audio_mkenum.py \
	gst-libs/gst/app/app_mkenum.py \
	gst-libs/gst/pbutils/pbutils_mkenum.py \
	gst-libs/gst/rtp/rtp_mkenum.py \
	gst-libs/gst/rtsp/rtsp_mkenum.py \
	gst-libs/gst/tag/tag_mkenum.py \
	gst-libs/gst/video/video_mkenum.py \
	gst-libs/gst/gl/gstglconfig.h.meson \
	meson_options.txt

DISTCLEANFILES = _stdint.h

ACLOCAL_AMFLAGS = -I m4 -I common/m4

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

check-valgrind:
	$(MAKE) -C tests/check check-valgrind

if HAVE_GST_CHECK
check-torture:
	$(MAKE) -C tests/check torture
build-checks:
	$(MAKE) -C tests/check build-checks
else
check-torture:
	true
build-checks:
	true
endif

include $(top_srcdir)/common/coverage/lcov.mak

# cruft: plugins that have been merged or moved or renamed

CRUFT_FILES = \
	$(top_builddir)/gst-plugins-base.spec \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool \
	$(top_builddir)/gst-libs/gst/audio/testchannels \
	$(top_builddir)/gst-libs/gst/app/gstapp-marshal.c \
	$(top_builddir)/gst-libs/gst/app/gstapp-marshal.h \
	$(top_builddir)/gst/encoding/.libs/libgstencodebin.so \
	$(top_builddir)/tests/check/elements/gdppay \
	$(top_builddir)/tests/check/elements/gdpdepay \
	$(top_builddir)/tests/check/pipelines/streamheader \
	$(top_builddir)/tests/examples/audio/testchannels \
	$(top_builddir)/tests/examples/volume/volume \
	$(top_builddir)/tools/gst-discoverer

CRUFT_DIRS = \
	$(top_srcdir)/docs/design \
	$(top_srcdir)/docs/plugins/tmpl \
	$(top_srcdir)/ext/gio \
	$(top_srcdir)/gst/gdp \
	$(top_srcdir)/sys/v4l \
	$(top_srcdir)/win32 \
	$(top_srcdir)/tests/examples/v4l

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
