LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

amrnb_LOCAL_SRC_FILES:= \
	amrnb.c \
	amrnbdec.c \
	amrnbenc.c 
 
LOCAL_SRC_FILES:= $(addprefix ../ext/amrnb/,$(amrnb_LOCAL_SRC_FILES))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.11     \
    libgstbase-0.11       \
    libglib-2.0           \
    libgthread-2.0        \
    libgmodule-2.0        \
    libgobject-2.0 		  \
	libopencore-amrnb                

LOCAL_MODULE:= libgstamrnb

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/..         	  		\
	$(LOCAL_PATH)      					\
    $(TARGET_OUT_HEADERS)/gstreamer-0.11 \
	$(TARGET_OUT_HEADERS)/glib-2.0 		\
    $(TARGET_OUT_HEADERS)/glib-2.0/glib \
	external/libxml2/include 	   		\
	$(TARGET_OUT_HEADERS)

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
