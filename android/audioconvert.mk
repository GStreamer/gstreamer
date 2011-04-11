LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

audioconvert_LOCAL_SRC_FILES:= \
	gst/audioconvert/gstaudioconvert.c \
	gst/audioconvert/audioconvert.c \
	gst/audioconvert/gstaudioconvertorc-dist.c \
	gst/audioconvert/gstchannelmix.c \
	gst/audioconvert/gstaudioquantize.c \
	gst/audioconvert/plugin.c 

LOCAL_SRC_FILES:= $(addprefix ../,$(audioconvert_LOCAL_SRC_FILES))

LOCAL_SHARED_LIBRARIES := \
    libgstaudio-0.11        \
    libgstreamer-0.11       \
    libgstbase-0.11         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 			\
    libgstpbutils-0.11

LOCAL_MODULE:= libgstaudioconvert

LOCAL_CFLAGS := -DHAVE_CONFIG_H -DGSTREAMER_BUILT_FOR_ANDROID \
	$(GST_PLUGINS_BASE_CFLAGS)
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

#It's a gstreamer plugins, and it must be installed on ..../lib/gstreamer-0.11
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/gstreamer-0.11
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
