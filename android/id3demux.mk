LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

id3demux_LOCAL_SRC_FILES:= \
	gst/id3demux/gstid3demux.c \
	gst/id3demux/id3tags.c \
	gst/id3demux/id3v2frames.c

LOCAL_SRC_FILES:= $(addprefix ../,$(id3demux_LOCAL_SRC_FILES))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.10       \
    libgstbase-0.10         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 			\
	libgsttag-0.10          \
	libgstpbutils-0.10

LOCAL_MODULE:= libgstid3demux

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../gst/id3demux  		\
    $(LOCAL_PATH)/..         			\
    $(LOCAL_PATH)/../gst-libs 			\
    $(LOCAL_PATH)  						\
	$(TARGET_OUT_HEADERS)/gstreamer-0.10 \
    $(TARGET_OUT_HEADERS)/glib-2.0      \
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

#It's a gstreamer plugins, and it must be installed on ..../lib/gstreamer-0.10
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/gstreamer-0.10

include $(BUILD_SHARED_LIBRARY)
