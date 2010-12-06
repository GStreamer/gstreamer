LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

wavenc_LOCAL_SRC_FILES:= \
	gst/wavenc/gstwavenc.c
 
LOCAL_SRC_FILES:= $(addprefix ../,$(wavenc_LOCAL_SRC_FILES))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.11       \
    libgstbase-0.11         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 			\
    libgstriff-0.11		

LOCAL_MODULE:= libgstwavenc

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../gst/wavenc  		\
    $(LOCAL_PATH)/..         	  		\
    $(LOCAL_PATH)/../gst-libs       	\
	$(LOCAL_PATH)      					\
    $(TARGET_OUT_HEADERS)/gstreamer-0.11 \
	$(TARGET_OUT_HEADERS)/glib-2.0 		\
    $(TARGET_OUT_HEADERS)/glib-2.0/glib \
	external/libxml2/include

ifeq ($(STECONF_ANDROID_VERSION),"FROYO")
LOCAL_SHARED_LIBRARIES += libicuuc 
LOCAL_C_INCLUDES += external/icu4c/common
endif

LOCAL_CFLAGS := -DHAVE_CONFIG_H	
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

#It's a gstreamer plugins, and it must be installed on ..../lib/gstreamer-0.11
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/gstreamer-0.11

include $(BUILD_SHARED_LIBRARY)
