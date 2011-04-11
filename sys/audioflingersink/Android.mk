# external/gstreamer/gstplayer/Android.mk
#
#  Copyright 2009 STN wireless
#
#ifeq ($(USE_HARDWARE_MM),true)

LOCAL_PATH:= $(call my-dir)

# -------------------------------------
# gstaudioflinger library
#
include $(CLEAR_VARS)
 
LOCAL_ARM_MODE := arm

gstaudioflinger_FILES := \
	 audioflinger_wrapper.cpp \
	 gstaudioflingersink.c \
	 GstAndroid.cpp 

LOCAL_SRC_FILES := $(gstaudioflinger_FILES)
LOCAL_C_INCLUDES = $(LOCAL_PATH) \
	$(LOCAL_PATH)/include \
	$(TOP)/frameworks/base

LOCAL_CFLAGS += -DHAVE_CONFIG_H
LOCAL_CFLAGS += -Wall -Wdeclaration-after-statement -g -O2
LOCAL_CFLAGS += -DANDROID_USE_GSTREAMER \
	$(shell $(PKG_CONFIG) gstreamer-plugins-bad-0.10 --cflags) \
	$(shell $(PKG_CONFIG) gstreamer-audio-0.10 --cflags)

ifeq ($(USE_AUDIO_PURE_CODEC),true)
LOCAL_CFLAGS += -DAUDIO_PURE_CODEC
endif

LOCAL_SHARED_LIBRARIES += libdl
LOCAL_SHARED_LIBRARIES += \
	libgstreamer-0.10     \
	libgstbase-0.10       \
	libglib-2.0           \
	libgthread-2.0        \
	libgmodule-2.0        \
	libgobject-2.0        \
	libgstvideo-0.10      \
	libgstaudio-0.10

LOCAL_LDFLAGS := -L$(SYSROOT)/usr/lib -llog

LOCAL_SHARED_LIBRARIES += \
	libutils \
	libcutils \
	libui \
	libhardware \
	libandroid_runtime \
	libmedia 


LOCAL_MODULE:= libgstaudioflinger
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/gstreamer-0.10

#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false 
LOCAL_MODULE_TAGS := eng debug

include $(BUILD_SHARED_LIBRARY)

#endif  # USE_HARDWARE_MM == true
