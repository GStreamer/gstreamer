DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS = \
	gst-libs gst sys ext pkgconfig \
	m4 common docs tests po tools

DIST_SUBDIRS = \
	gst gst-libs sys ext pkgconfig \
	m4 common docs tests po tools

# include before EXTRA_DIST for win32 assignment
include $(top_srcdir)/common/win32.mak

EXTRA_DIST = \
	gst-plugins-bad.spec depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
	ChangeLog gst-plugins-bad.doap autogen.sh \
	$(win32)

DISTCLEANFILES = _stdint.h

ACLOCAL_AMFLAGS = -I m4 -I common/m4

include $(top_srcdir)/common/release.mak
include $(top_srcdir)/common/po.mak

check-valgrind:
	cd tests/check && make check-valgrind

if HAVE_GST_CHECK
check-torture:
	cd tests/check && make torture
else
check-torture:
	true
endif

win32-update:
	cp $(top_builddir)/win32/common/config.h-new \
	    $(top_srcdir)/win32/common/config.h

include $(top_srcdir)/common/coverage/lcov.mak

check: check-exports

# cruft: plugins that have been merged or moved or renamed

CRUFT_FILES = \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool \
	$(top_builddir)/ext/alsaspdif/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/ext/ivorbis/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/ext/jack/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/aacparse/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/amrparse/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/audioparsers/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/flacparse/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/imagefreeze/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/mpeg4videoparse/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/qtmux/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/rtpmux/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/rtpvp8/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/scaletempo/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/selector/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/shapewipe/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/valve/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/videoparsers/.libs/libgsth263parse* \
	$(top_builddir)/gst-libs/gst/baseparse/.libs/libgstbaseparse* \
	$(top_builddir)/sys/oss4/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/tests/check/elements/{aac,ac3,amr,flac,mpegaudio,dca}parse \
	$(top_builddir)/tests/check/elements/autocolorspace \
	$(top_builddir)/tests/check/elements/capssetter \
	$(top_builddir)/tests/check/elements/imagefreeze \
	$(top_builddir)/tests/check/elements/qtmux \
	$(top_builddir)/tests/check/elements/rtpmux \
	$(top_builddir)/tests/check/elements/selector \
	$(top_builddir)/tests/check/elements/valve \
	$(top_builddir)/tests/check/pipelines/metadata \
	$(top_builddir)/tests/examples/jack/jack_client \
	$(top_builddir)/tests/examples/scaletempo/scaletempo-demo \
	$(top_builddir)/tests/examples/switch/switcher \
	$(top_builddir)/tests/icles/output-selector-test \
	$(top_builddir)/tests/icles/test-oss4 \
	$(top_builddir)/tests/icles/equalizer-test

CRUFT_DIRS = \
	$(top_srcdir)/gst/aacparse \
	$(top_srcdir)/gst/amrparse \
	$(top_srcdir)/gst/camerabin \
	$(top_srcdir)/gst/flacparse \
	$(top_srcdir)/gst/hls \
	$(top_srcdir)/gst/imagefreeze \
	$(top_srcdir)/gst/invtelecine \
	$(top_srcdir)/gst/mpeg4videoparse \
	$(top_srcdir)/gst/qtmux \
	$(top_srcdir)/gst/rtpmux \
	$(top_srcdir)/gst/rtpvp8 \
	$(top_srcdir)/gst/scaletempo \
	$(top_srcdir)/gst/selector \
	$(top_srcdir)/gst/shapewipe \
	$(top_srcdir)/gst/valve \
	$(top_srcdir)/gst-libs/gst/baseparse \
	$(top_srcdir)/gst-libs/gst/egl \
	$(top_srcdir)/gst-libs/gst/signalprocessor \
	$(top_srcdir)/tests/examples/scaletempo \
	$(top_srcdir)/tests/examples/shapewipe \
	$(top_srcdir)/tests/examples/switch \
	$(top_srcdir)/tests/examples/jack \
	$(top_srcdir)/ext/alsaspdif \
	$(top_srcdir)/ext/amrwbenc \
	$(top_srcdir)/ext/cog \
	$(top_srcdir)/ext/eglgles \
	$(top_srcdir)/ext/ivorbis \
	$(top_srcdir)/ext/jack \
	$(top_srcdir)/ext/metadata \
	$(top_srcdir)/ext/swfdec \
	$(top_srcdir)/ext/tarkin \
	$(top_srcdir)/ext/theora \
	$(top_srcdir)/ext/vp8 \
	$(top_srcdir)/sys/mfc

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
