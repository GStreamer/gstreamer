LOCAL_PATH := $(call my-dir)

GSTREAMER_TOP := $(LOCAL_PATH)

include $(CLEAR_VARS)

#plugins
include $(GSTREAMER_TOP)/android/qtdemux.mk
include $(GSTREAMER_TOP)/android/avi.mk
include $(GSTREAMER_TOP)/android/wavparse.mk
include $(GSTREAMER_TOP)/android/rtp.mk
include $(GSTREAMER_TOP)/android/rtsp.mk
include $(GSTREAMER_TOP)/android/id3demux.mk
include $(GSTREAMER_TOP)/android/udp.mk
include $(GSTREAMER_TOP)/android/flv.mk
include $(GSTREAMER_TOP)/android/soup.mk
include $(GSTREAMER_TOP)/android/rtpmanager.mk
include $(GSTREAMER_TOP)/android/icydemux.mk
include $(GSTREAMER_TOP)/android/wavenc.mk
include $(GSTREAMER_TOP)/android/apetag.mk
