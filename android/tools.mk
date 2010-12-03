LOCAL_PATH := $(call my-dir)

GSTREAMER_TOP := $(LOCAL_PATH)

include $(CLEAR_VARS)

include $(GSTREAMER_TOP)/gst-inspect.mk
include $(GSTREAMER_TOP)/gst-launch.mk
include $(GSTREAMER_TOP)/gst-plugin-scanner.mk
