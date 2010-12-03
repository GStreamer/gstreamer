LOCAL_PATH := $(call my-dir)

GSTREAMER_TOP := $(LOCAL_PATH)

include $(CLEAR_VARS)
#dependancy library 
#include $(GSTREAMER_TOP)/android/amrnb_library.mk
#plugins
include $(GSTREAMER_TOP)/android/mpegaudioparse.mk
include $(GSTREAMER_TOP)/android/asf.mk
#include $(GSTREAMER_TOP)/android/amrnb.mk
#include $(GSTREAMER_TOP)/android/amrwbdec.mk
