DISTCHECK_CONFIGURE_FLAGS=--enable-gtk-doc

SUBDIRS = \
	gst-libs gst sys ext pkgconfig \
	m4 common docs tests po tools

DIST_SUBDIRS = \
	gst gst-libs sys ext pkgconfig \
	m4 common docs tests po tools

EXTRA_DIST = \
	depcomp \
	AUTHORS COPYING NEWS README RELEASE REQUIREMENTS \
	ChangeLog gst-plugins-bad.doap autogen.sh \
	$(shell find "$(top_srcdir)" -type f -name meson.build ! -path "$(top_srcdir)/$(PACKAGE_TARNAME)-*" ) \
	meson_options.txt \
	gst-libs/gst/interfaces/build_mkenum.py \
	gst-libs/gst/mpegts/mpegts_enum.py \
	gst-libs/gst/webrtc/webrtc_mkenum.py \
	ext/srtp/srtp_mkenum.py \
	ext/vulkan/vkconfig.h.meson

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
	$(top_builddir)/gst-plugins-bad.spec \
	$(top_builddir)/common/shave \
	$(top_builddir)/common/shave-libtool \
	$(top_builddir)/docs/plugins/xml/plugin-sdp.xml \
	$(top_builddir)/ext/alsaspdif/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/ext/gtk/.libs/libgstgtksink.so \
	$(top_builddir)/ext/gtk/.libs/libgstgtk.so \
	$(top_builddir)/ext/hls/.libs/libgstfragmented* \
	$(top_builddir)/ext/ivorbis/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/ext/jack/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/ext/libde265/.libs/libgstlibde265.{so,dll,DLL,dylib} \
	$(top_builddir)/ext/teletextdec/.libs/libgstteletextdec.so \
	$(top_builddir)/gst/opus/.libs/libgstopus.{so,dll,DLL,dylib} \
	$(top_builddir)/ext/mpg123/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/ext/qt/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/ext/schroedinger/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/aacparse/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/amrparse/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/audiomixer/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/audioparsers/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/camerabin2/.libs/libgstcamerabin2.so \
	$(top_builddir)/gst/flacparse/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/imagefreeze/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/mpeg4videoparse/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/qtmux/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/rawparse/.libs/libgstrawparse* \
	$(top_builddir)/gst/rtp/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/rtpmux/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/rtpvp8/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/scaletempo/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/selector/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/shapewipe/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/valve/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/gst/videoparsers/.libs/libgsth263parse* \
	$(top_builddir)/gst-libs/gst/baseparse/.libs/libgstbaseparse* \
	$(top_builddir)/sys/d3dvideosink/.libs/libgstd3dvideosink.{dll,DLL} \
	$(top_builddir)/sys/oss4/.libs/*.{so,dll,DLL,dylib} \
	$(top_builddir)/tests/check/elements/{aac,ac3,amr,flac,mpegaudio,dca}parse \
	$(top_builddir)/tests/check/elements/autocolorspace \
	$(top_builddir)/tests/check/elements/capssetter \
	$(top_builddir)/tests/check/elements/dataurisrc \
	$(top_builddir)/tests/check/elements/imagefreeze \
	$(top_builddir)/tests/check/elements/qtmux \
	$(top_builddir)/tests/check/elements/rawaudioparse \
	$(top_builddir)/tests/check/elements/rawvideoparse \
	$(top_builddir)/tests/check/elements/rtpmux \
	$(top_builddir)/tests/check/elements/schroenc \
	$(top_builddir)/tests/check/elements/selector \
	$(top_builddir)/tests/check/elements/valve \
	$(top_builddir)/tests/check/pipelines/metadata \
	$(top_builddir)/tests/check/pipelines/mimic \
	$(top_builddir)/tests/examples/jack/jack_client \
	$(top_builddir)/tests/examples/scaletempo/scaletempo-demo \
	$(top_builddir)/tests/examples/switch/switcher \
	$(top_builddir)/tests/icles/output-selector-test \
	$(top_builddir)/tests/icles/test-oss4 \
	$(top_builddir)/tests/icles/equalizer-test

CRUFT_DIRS = \
	$(top_srcdir)/docs/plugins/tmpl \
	$(top_srcdir)/gst/aacparse \
	$(top_srcdir)/gst/amrparse \
	$(top_srcdir)/gst/audiomixer \
	$(top_srcdir)/gst/camerabin \
	$(top_srcdir)/gst/dataurisrc \
	$(top_srcdir)/gst/flacparse \
	$(top_srcdir)/gst/hls \
	$(top_srcdir)/gst/imagefreeze \
	$(top_srcdir)/gst/invtelecine \
	$(top_srcdir)/gst/liveadder \
	$(top_srcdir)/gst/mpeg4videoparse \
	$(top_srcdir)/gst/qtmux \
	$(top_srcdir)/gst/rtpmux \
	$(top_srcdir)/gst/rtpvp8 \
	$(top_srcdir)/gst/rtp \
	$(top_srcdir)/gst/scaletempo \
	$(top_srcdir)/gst/selector \
	$(top_srcdir)/gst/shapewipe \
	$(top_srcdir)/gst/valve \
	$(top_srcdir)/gst-libs/gst/baseparse \
	$(top_srcdir)/gst-libs/gst/base \
	$(top_srcdir)/gst-libs/gst/egl \
	$(top_srcdir)/gst-libs/gst/gl \
	$(top_srcdir)/gst-libs/gst/signalprocessor \
	$(top_srcdir)/tests/examples/gl \
	$(top_srcdir)/tests/examples/scaletempo \
	$(top_srcdir)/tests/examples/gtk \
	$(top_srcdir)/tests/examples/qt \
	$(top_srcdir)/tests/examples/shapewipe \
	$(top_srcdir)/tests/examples/switch \
	$(top_srcdir)/tests/examples/jack \
	$(top_srcdir)/ext/alsaspdif \
	$(top_srcdir)/ext/amrwbenc \
	$(top_srcdir)/ext/cog \
	$(top_srcdir)/ext/eglgles \
	$(top_srcdir)/ext/gsettings \
	$(top_srcdir)/ext/gtk \
	$(top_srcdir)/ext/ivorbis \
	$(top_srcdir)/ext/jack \
	$(top_srcdir)/ext/metadata \
	$(top_srcdir)/ext/mimic \
	$(top_srcdir)/ext/mpg123 \
	$(top_srcdir)/ext/qt \
	$(top_srcdir)/ext/schroedinger \
	$(top_srcdir)/ext/swfdec \
	$(top_srcdir)/ext/tarkin \
	$(top_srcdir)/ext/theora \
	$(top_srcdir)/ext/vp8 \
	$(top_srcdir)/sys/mfc \
	$(top_srcdir)/win32

include $(top_srcdir)/common/cruft.mak

all-local: check-cruft
