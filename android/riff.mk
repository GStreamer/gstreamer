LOCAL_PATH:= $(call my-dir)
#----------------------------------------
# include 
gst_riffs_COPY_HEADERS_TO := gstreamer-0.10/gst/riff
gst_riffs_COPY_HEADERS := \
	../gst-libs/gst/riff/riff-ids.h \
	../gst-libs/gst/riff/riff-media.h \
	../gst-libs/gst/riff/riff-read.h

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

riff_LOCAL_SRC_FILES:= \
	gst-libs/gst/riff/riff.c \
	gst-libs/gst/riff/riff-media.c \
	gst-libs/gst/riff/riff-read.c 
    
LOCAL_SRC_FILES:= $(addprefix ../,$(riff_LOCAL_SRC_FILES))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.10       \
    libgstbase-0.10         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 			\
	libgsttag-0.10          \
    libgstaudio-0.10     


LOCAL_MODULE:= libgstriff-0.10

LOCAL_CFLAGS := -DHAVE_CONFIG_H	 -DGSTREAMER_BUILT_FOR_ANDROID \
	$(GST_PLUGINS_BASE_CFLAGS)
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

LOCAL_COPY_HEADERS_TO := $(gst_riffs_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_riffs_COPY_HEADERS)
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
