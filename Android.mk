LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

GST_PLUGINS_BAD_TOP := $(LOCAL_PATH)

GST_PLUGINS_BAD_BUILT_SOURCES := \
	pkgconfig/gstreamer-plugins-bad-0.10-uninstalled.pc 	\
	pkgconfig/gstreamer-plugins-bad-0.10.pc \
	gst-libs/gst/baseparse/Android.mk \
	gst-libs/gst/basecamerabinsrc/Android.mk \
	gst-libs/gst/interfaces/Android.mk \
	gst/h264parse/Android.mk \
	gst/videoparsers/Android.mk \
	gst/audiobuffer/Android.mk \
	gst/autoconvert/Android.mk \
	gst/bayer/Android.mk \
	gst/camerabin2/Android.mk \
	gst/adpcmdec/Android.mk \
	gst/adpcmenc/Android.mk \
	gst/aiff/Android.mk \
	gst/asfmux/Android.mk \
	gst/sdp/Android.mk \
	gst/hls/Android.mk \
	gst/jp2kdecimator/Android.mk \
	gst/segmentclip/Android.mk \
	gst/dtmf/Android.mk \
	gst/mpeg4videoparse/Android.mk \
	gst/siren/Android.mk \
	gst/dataurisrc/Android.mk \
	gst/rawparse/Android.mk \
	gst/videomaxrate/Android.mk \
	gst/tta/Android.mk \
	gst/videosignal/Android.mk \
	gst/coloreffects/Android.mk \
	gst/scaletempo/Android.mk \
	gst/jpegformat/Android.mk \
	gst/freeze/Android.mk \
	gst/geometrictransform/Android.mk \
	gst/librfb/Android.mk \
	gst/vmnc/Android.mk \
	gst/interlace/Android.mk \
	gst/mxf/Android.mk \
	gst/cdxaparse/Android.mk \
	gst/mpegpsmux/Android.mk \
	gst/legacyresample/Android.mk \
	gst/gaudieffects/Android.mk \
	gst/liveadder/Android.mk \
	gst/nsf/Android.mk \
	gst/dvdspu/Android.mk \
	gst/mpegvideoparse/Android.mk \
	gst/mpegtsdemux/Android.mk \
	gst/debugutils/Android.mk \
	gst/subenc/Android.mk \
	gst/id3tag/Android.mk \
	gst/frei0r/Android.mk \
	gst/patchdetect/Android.mk \
	gst/speed/Android.mk \
	gst/sdi/Android.mk \
	gst/festival/Android.mk \
	gst/y4m/Android.mk \
	gst/rtpmux/Android.mk \
	gst/pcapparse/Android.mk \
	gst/nuvdemux/Android.mk \
	gst/colorspace/Android.mk \
	gst/pnm/Android.mk \
	gst/mve/Android.mk \
	gst/videomeasure/Android.mk \
	gst/invtelecine/Android.mk \
	gst/hdvparse/Android.mk \
	gst/stereo/Android.mk \
	gst/rtpvp8/Android.mk \
	gst/mpegdemux/Android.mk \
	gst/ivfparse/Android.mk \
	ext/faad/Android.mk

GST_PLUGINS_BAD_BUILT_SOURCES := $(patsubst %, $(abspath $(GST_PLUGINS_BAD_TOP))/%, $(GST_PLUGINS_BAD_BUILT_SOURCES))


.PHONY: gst-plugins-bad-configure gst-plugins-bad-configure-real
gst-plugins-bad-configure:
	cd $(GST_PLUGINS_BAD_TOP) ; \
	CC="$(CONFIGURE_CC)" \
	CFLAGS="$(CONFIGURE_CFLAGS)" \
	LD=$(TARGET_LD) \
	LDFLAGS="$(CONFIGURE_LDFLAGS)" \
	CPP=$(CONFIGURE_CPP) \
	CPPFLAGS="$(CONFIGURE_CPPFLAGS)" \
	PKG_CONFIG_LIBDIR="$(CONFIGURE_PKG_CONFIG_LIBDIR)" \
	PKG_CONFIG_TOP_BUILD_DIR=/ \
	$(abspath $(GST_PLUGINS_BAD_TOP))/$(CONFIGURE) \
		--prefix=/system --host=arm-linux-androideabi --disable-gtk-doc \
		--disable-valgrind && \
	for file in $(GST_PLUGINS_BAD_BUILT_SOURCES); do \
		rm -f $$file && \
		make -C $$(dirname $$file) $$(basename $$file) ; \
	done

CONFIGURE_TARGETS += gst-plugins-bad-configure

-include $(GST_PLUGINS_BAD_TOP)/gst-libs/gst/baseparse/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst-libs/gst/basecamerabinsrc/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst-libs/gst/interfaces/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/h264parse/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/audiobuffer/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/autoconvert/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/bayer/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/camerabin2/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/adpcmdec/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/adpcmenc/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/aiff/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/asfmux/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/videoparsers/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/ext/faad/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/sys/audioflingersink/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/sdp/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/hls/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/jp2kdecimator/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/segmentclip/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/dtmf/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/mpeg4videoparse/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/siren/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/dataurisrc/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/rawparse/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/videomaxrate/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/tta/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/videosignal/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/coloreffects/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/scaletempo/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/jpegformat/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/freeze/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/geometrictransform/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/librfb/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/vmnc/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/interlace/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/mxf/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/cdxaparse/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/mpegpsmux/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/legacyresample/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/gaudieffects/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/liveadder/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/nsf/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/dvdspu/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/mpegvideoparse/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/mpegtsdemux/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/debugutils/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/subenc/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/id3tag/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/frei0r/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/patchdetect/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/speed/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/sdi/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/festival/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/y4m/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/rtpmux/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/pcapparse/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/nuvdemux/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/colorspace/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/pnm/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/mve/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/videomeasure/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/invtelecine/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/hdvparse/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/stereo/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/rtpvp8/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/mpegdemux/Android.mk
-include $(GST_PLUGINS_BAD_TOP)/gst/ivfparse/Android.mk
