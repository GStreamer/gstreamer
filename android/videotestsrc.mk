LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

videotestsrc_LOCAL_SRC_FILES:= \
	gst/videotestsrc/gstvideotestsrc.c \
	gst/videotestsrc/videotestsrc.c \
	gst/videotestsrc/gstvideotestsrcorc-dist.c \


LOCAL_SRC_FILES:= $(addprefix ../,$(videotestsrc_LOCAL_SRC_FILES))

LOCAL_SHARED_LIBRARIES := \
    libgstcontroller-0.10   \
    libgstvideo-0.10        \
    libgstreamer-0.10       \
    libgstbase-0.10         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0

LOCAL_MODULE:= libgstvideotestsrc

LOCAL_CFLAGS := -DHAVE_CONFIG_H -DGSTREAMER_BUILT_FOR_ANDROID \
	$(GST_PLUGINS_BASE_CFLAGS)
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

#It's a gstreamer plugins, and it must be installed on ..../lib/gstreamer-0.10
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/gstreamer-0.10
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
