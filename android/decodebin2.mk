LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

decodebin2_LOCAL_SRC_FILES_BASE:= \
	gst/playback/gstdecodebin2.c \
    gst/playback/gsturidecodebin.c \
	gst/playback/gstplay-enum.c  \
	gst/playback/gstplay-marshal.c

LOCAL_SRC_FILES:= $(addprefix ../,$(decodebin2_LOCAL_SRC_FILES_BASE))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.10       \
    libgstbase-0.10         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 			\
	libgstpbutils-0.10

LOCAL_MODULE:= libgstdecodebin2

LOCAL_CFLAGS := -DHAVE_CONFIG_H	 \
	$(GST_PLUGINS_BASE_CFLAGS)
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

#It's a gstreamer plugins, and it must be installed on ..../lib/gstreamer-0.10
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/gstreamer-0.10
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
