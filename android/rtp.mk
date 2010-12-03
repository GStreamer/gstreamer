LOCAL_PATH:= $(call my-dir)
#----------------------------------------
# include 
gst_rtp_COPY_HEADERS_TO := gstreamer-0.10/gst/rtp
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
    libgstreamer-0.10       \
    libgstbase-0.10         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 		

LOCAL_MODULE:= libgstrtp-0.10

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../gst-libs/gst/rtp   \
    $(LOCAL_PATH)/../gst-libs      		\
    $(LOCAL_PATH)/../         			\
    $(LOCAL_PATH)   	  				\
	$(LOCAL_PATH)/gst/tcp    			\
    $(TARGET_OUT_HEADERS)/gstreamer-0.10 \
	$(TARGET_OUT_HEADERS)/glib-2.0 		\
    $(TARGET_OUT_HEADERS)/glib-2.0/glib \
	external/libxml2/include

ifeq ($(STECONF_ANDROID_VERSION),"FROYO")
LOCAL_SHARED_LIBRARIES += libicuuc 
LOCAL_C_INCLUDES += external/icu4c/common
endif

LOCAL_CFLAGS := -DHAVE_CONFIG_H	-DGSTREAMER_BUILT_FOR_ANDROID
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

LOCAL_COPY_HEADERS_TO := $(gst_rtp_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_rtp_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
