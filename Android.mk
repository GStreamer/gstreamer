LOCAL_PATH := $(call my-dir)

GSTREAMER_TOP := $(LOCAL_PATH)

include $(CLEAR_VARS)

include $(GSTREAMER_TOP)/android/h264parse.mk
include $(GSTREAMER_TOP)/android/sdpelem.mk
include $(GSTREAMER_TOP)/android/metadata.mk
include $(GSTREAMER_TOP)/android/qtmux.mk
include $(GSTREAMER_TOP)/android/aacparse.mk
include $(GSTREAMER_TOP)/android/amrparse.mk
