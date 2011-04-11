LOCAL_PATH:= $(call my-dir)
#----------------------------------------
# include 
gst_rtp_COPY_HEADERS_TO := gstreamer-0.11/gst/rtp
gst_rtp_COPY_HEADERS := \
	../gst-libs/gst/rtp/gstbasertpaudiopayload.h \
	../gst-libs/gst/rtp/gstbasertpdepayload.h \
	../gst-libs/gst/rtp/gstbasertppayload.h \
	../gst-libs/gst/rtp/gstrtcpbuffer.h \
	../gst-libs/gst/rtp/gstrtpbuffer.h \
	../gst-libs/gst/rtp/gstrtppayloads.h

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

rtp_LOCAL_SRC_FILES:= \
	gst-libs/gst/rtp/gstrtpbuffer.c \
	gst-libs/gst/rtp/gstrtcpbuffer.c \
	gst-libs/gst/rtp/gstrtppayloads.c \
	gst-libs/gst/rtp/gstbasertpaudiopayload.c \
	gst-libs/gst/rtp/gstbasertppayload.c \
	gst-libs/gst/rtp/gstbasertpdepayload.c 

LOCAL_SRC_FILES:= $(addprefix ../,$(rtp_LOCAL_SRC_FILES))

LOCAL_SHARED_LIBRARIES := \
	libdl                   \
    libgstreamer-0.11       \
    libgstbase-0.11         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 		

LOCAL_MODULE:= libgstrtp-0.11
LOCAL_CFLAGS := -DHAVE_CONFIG_H	-DGSTREAMER_BUILT_FOR_ANDROID \
	$(GST_PLUGINS_BASE_CFLAGS)
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

LOCAL_COPY_HEADERS_TO := $(gst_rtp_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_rtp_COPY_HEADERS)
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
