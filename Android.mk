LOCAL_PATH := $(call my-dir)

GSTREAMER_TOP := $(LOCAL_PATH)

include $(CLEAR_VARS)

include $(GSTREAMER_TOP)/android/tag.mk
include $(GSTREAMER_TOP)/android/audio.mk
include $(GSTREAMER_TOP)/android/video.mk
include $(GSTREAMER_TOP)/android/riff.mk
include $(GSTREAMER_TOP)/android/interfaces.mk
include $(GSTREAMER_TOP)/android/pbutils.mk
include $(GSTREAMER_TOP)/android/rtp.mk
include $(GSTREAMER_TOP)/android/rtsp.mk
include $(GSTREAMER_TOP)/android/netbuffer.mk
include $(GSTREAMER_TOP)/android/sdp.mk
include $(GSTREAMER_TOP)/android/app.mk
#plugins
include $(GSTREAMER_TOP)/android/alsa.mk
include $(GSTREAMER_TOP)/android/decodebin.mk
include $(GSTREAMER_TOP)/android/decodebin2.mk
#include $(GSTREAMER_TOP)/android/queue2.mk
include $(GSTREAMER_TOP)/android/playbin.mk
include $(GSTREAMER_TOP)/android/typefindfunctions.mk
include $(GSTREAMER_TOP)/android/app_plugin.mk
include $(GSTREAMER_TOP)/android/gdp.mk
include $(GSTREAMER_TOP)/android/tcp.mk
include $(GSTREAMER_TOP)/android/audioconvert.mk

