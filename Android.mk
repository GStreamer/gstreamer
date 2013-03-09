LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

GST_PLUGINS_GOOD_TOP := $(LOCAL_PATH)

GST_PLUGINS_GOOD_BUILT_SOURCES = \
	gst/alpha/Android.mk		\
	gst/apetag/Android.mk		\
	gst/audiofx/Android.mk		\
	gst/auparse/Android.mk		\
	gst/autodetect/Android.mk	\
	gst/avi/Android.mk		\
	gst/cutter/Android.mk		\
	gst/debugutils/Android.mk	\
	gst/deinterlace/Android.mk	\
	gst/dtmf/Android.mk         \
	gst/effectv/Android.mk		\
	gst/equalizer/Android.mk	\
	gst/flv/Android.mk		\
	gst/flx/Android.mk		\
	gst/goom/Android.mk		\
	gst/goom2k1/Android.mk		\
	gst/icydemux/Android.mk		\
	gst/id3demux/Android.mk		\
	gst/imagefreeze/Android.mk	\
	gst/interleave/Android.mk	\
	gst/law/Android.mk		\
	gst/level/Android.mk		\
	gst/matroska/Android.mk		\
	gst/multifile/Android.mk	\
	gst/multipart/Android.mk	\
	gst/isomp4/Android.mk	\
	gst/replaygain/Android.mk	\
	gst/rtp/Android.mk		\
	gst/rtpmanager/Android.mk	\
	gst/rtsp/Android.mk		\
	gst/shapewipe/Android.mk	\
	gst/smpte/Android.mk		\
	gst/spectrum/Android.mk		\
	gst/udp/Android.mk		\
	gst/videobox/Android.mk		\
	gst/videocrop/Android.mk	\
	gst/videofilter/Android.mk	\
	gst/videomixer/Android.mk	\
	gst/wavenc/Android.mk		\
	gst/monoscope/Android.mk		\
	gst/y4m/Android.mk		\
	gst/wavparse/Android.mk

GST_PLUGINS_GOOD_BUILT_SOURCES := $(patsubst %, $(abspath $(GST_PLUGINS_GOOD_TOP))/%, $(GST_PLUGINS_GOOD_BUILT_SOURCES))

.PHONY: gst-plugins-good-configure gst-plugins-good-configure-real
gst-plugins-good-configure-real:
	cd $(GST_PLUGINS_GOOD_TOP) ; \
	CC="$(CONFIGURE_CC)" \
	CFLAGS="$(CONFIGURE_CFLAGS)" \
	LD=$(TARGET_LD) \
	LDFLAGS="$(CONFIGURE_LDFLAGS)" \
	CPP=$(CONFIGURE_CPP) \
	CPPFLAGS="$(CONFIGURE_CPPFLAGS)" \
	PKG_CONFIG_LIBDIR="$(CONFIGURE_PKG_CONFIG_LIBDIR)" \
	PKG_CONFIG_TOP_BUILD_DIR=/ \
	$(abspath $(GST_PLUGINS_GOOD_TOP))/$(CONFIGURE) --host=arm-linux-androideabi \
	--prefix=/system --disable-orc --disable-valgrind --disable-gtk-doc && \
	for file in $(GST_PLUGINS_GOOD_BUILT_SOURCES); do \
		rm -f $$file && \
		make -C $$(dirname $$file) $$(basename $$file) ; \
	done

gst-plugins-good-configure: gst-plugins-good-configure-real

CONFIGURE_TARGETS += gst-plugins-good-configure

-include $(GST_PLUGINS_GOOD_TOP)/gst/alpha/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/apetag/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/audiofx/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/auparse/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/autodetect/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/avi/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/cutter/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/debugutils/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/deinterlace/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/dtmf/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/effectv/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/equalizer/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/flv/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/flx/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/goom/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/goom2k1/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/icydemux/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/id3demux/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/imagefreeze/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/interleave/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/law/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/level/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/matroska/Android.mk
#-include $(GST_PLUGINS_GOOD_TOP)/gst/multifile/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/multipart/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/isomp4/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/replaygain/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/rtp/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/rtpmanager/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/rtsp/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/shapewipe/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/smpte/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/spectrum/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/udp/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/videobox/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/videocrop/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/videofilter/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/videomixer/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/wavenc/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/wavparse/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/y4m/Android.mk
-include $(GST_PLUGINS_GOOD_TOP)/gst/monoscope/Android.mk
