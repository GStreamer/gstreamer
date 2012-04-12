LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

GST_PLUGINS_BASE_TOP := $(LOCAL_PATH)

GST_PLUGINS_BASE_BUILT_SOURCES := 						\
	pkgconfig/gstreamer-app-1.0.pc				\
	pkgconfig/gstreamer-audio-1.0.pc			\
	pkgconfig/gstreamer-fft-1.0.pc				\
	pkgconfig/gstreamer-interfaces-1.0.pc			\
	pkgconfig/gstreamer-pbutils-1.0.pc			\
	pkgconfig/gstreamer-plugins-base-1.0.pc		\
	pkgconfig/gstreamer-riff-1.0.pc			\
	pkgconfig/gstreamer-rtp-1.0.pc				\
	pkgconfig/gstreamer-rtsp-1.0.pc			\
	pkgconfig/gstreamer-sdp-1.0.pc				\
	pkgconfig/gstreamer-tag-1.0.pc				\
	pkgconfig/gstreamer-video-1.0.pc			\
	pkgconfig/gstreamer-app-1.0-uninstalled.pc		\
	pkgconfig/gstreamer-audio-1.0-uninstalled.pc		\
	pkgconfig/gstreamer-fft-1.0-uninstalled.pc		\
	pkgconfig/gstreamer-interfaces-1.0-uninstalled.pc	\
	pkgconfig/gstreamer-pbutils-1.0-uninstalled.pc		\
	pkgconfig/gstreamer-plugins-base-1.0-uninstalled.pc	\
	pkgconfig/gstreamer-riff-1.0-uninstalled.pc		\
	pkgconfig/gstreamer-rtp-1.0-uninstalled.pc		\
	pkgconfig/gstreamer-rtsp-1.0-uninstalled.pc		\
	pkgconfig/gstreamer-sdp-1.0-uninstalled.pc		\
	pkgconfig/gstreamer-tag-1.0-uninstalled.pc		\
	pkgconfig/gstreamer-video-1.0-uninstalled.pc		\
	gst-libs/gst/interfaces/Android.mk			\
	gst-libs/gst/tag/Android.mk				\
	gst-libs/gst/audio/Android.mk				\
	gst-libs/gst/video/Android.mk				\
	gst-libs/gst/riff/Android.mk				\
	gst-libs/gst/pbutils/Android.mk				\
	gst-libs/gst/rtp/Android.mk				\
	gst-libs/gst/rtsp/Android.mk				\
	gst-libs/gst/sdp/Android.mk				\
	gst-libs/gst/app/Android.mk				\
	gst-libs/gst/fft/Android.mk				\
	gst/playback/Android.mk					\
	gst/typefind/Android.mk					\
	gst/app/Android.mk					\
	gst/gdp/Android.mk					\
	gst/tcp/Android.mk					\
	gst/audioconvert/Android.mk				\
	gst/audioresample/Android.mk				\
	gst/audiotestsrc/Android.mk				\
	gst/videotestsrc/Android.mk				\
	gst/videoscale/Android.mk				\
	gst/videoconvert/Android.mk				\
	gst/videorate/Android.mk				\
	gst/encoding/Android.mk					\
	gst/adder/Android.mk					\
	gst/audiorate/Android.mk					\
	gst/volume/Android.mk					\
	tools/Android.mk					\
	ext/ogg/Android.mk

ifneq ($(NDK_BUILD), true)
GST_PLUGINS_BASE_BUILT_SOURCES += ext/vorbis/Android.mk
ZLIB_CFLAGS = -I $(abspath $(GST_PLUGINS_BASE_TOP)/../../zlib)
endif

GST_PLUGINS_BASE_BUILT_SOURCES := $(patsubst %, $(abspath $(GST_PLUGINS_BASE_TOP))/%, $(GST_PLUGINS_BASE_BUILT_SOURCES))


.PHONY: gst-plugins-base-configure
gst-plugins-base-configure:
	cd $(GST_PLUGINS_BASE_TOP) ; \
	CC="$(CONFIGURE_CC)" \
	CFLAGS="$(CONFIGURE_CFLAGS)" \
	LD=$(TARGET_LD) \
	LDFLAGS="$(CONFIGURE_LDFLAGS)" \
	CPP=$(CONFIGURE_CPP) \
	CPPFLAGS="$(CONFIGURE_CPPFLAGS)" \
	PKG_CONFIG_LIBDIR="$(CONFIGURE_PKG_CONFIG_LIBDIR)" \
	PKG_CONFIG_TOP_BUILD_DIR=/ \
	IVORBIS_CFLAGS="-I$(TOP)/external/tremolo -DTREMOR" \
	IVORBIS_LIBS="-lvorbisidec" \
	ZLIB_CFLAGS="$(ZLIB_CFLAGS)" \
	$(abspath $(GST_PLUGINS_BASE_TOP))/$(CONFIGURE) --host=arm-linux-androideabi \
	--prefix=/system --disable-orc --disable-gio --enable-ivorbis \
	--disable-valgrind --disable-gtk-doc && \
	for file in $(GST_PLUGINS_BASE_BUILT_SOURCES); do \
		rm -f $$file && \
		make -C $$(dirname $$file) $$(basename $$file) ; \
	done

CONFIGURE_TARGETS += gst-plugins-base-configure

-include $(GST_PLUGINS_BASE_TOP)/gst-libs/gst/interfaces/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst-libs/gst/fft/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst-libs/gst/tag/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst-libs/gst/audio/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst-libs/gst/video/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst-libs/gst/riff/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst-libs/gst/pbutils/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst-libs/gst/rtp/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst-libs/gst/rtsp/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst-libs/gst/sdp/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst-libs/gst/app/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/playback/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/typefind/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/app/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/gdp/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/tcp/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/audioconvert/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/audioresample/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/audiotestsrc/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/videotestsrc/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/videoscale/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/videoconvert/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/videorate/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/encoding/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/adder/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/audiorate/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/gst/volume/Android.mk
-include $(GST_PLUGINS_BASE_TOP)/ext/ogg/Android.mk
ifneq ($(NDK_BUILD), true)
-include $(GST_PLUGINS_BASE_TOP)/ext/vorbis/Android.mk
endif
-include $(GST_PLUGINS_BASE_TOP)/tools/Android.mk
