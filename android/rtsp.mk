LOCAL_PATH:= $(call my-dir)
#----------------------------------------
# include 
gst_rtsp_COPY_HEADERS_TO := gstreamer-0.11/gst/rtsp
gst_rtsp_COPY_HEADERS_BASE := \
	gst-libs/gst/rtsp/gstrtspbase64.h \
	gst-libs/gst/rtsp/gstrtspdefs.h \
	gst-libs/gst/rtsp/gstrtspconnection.h \
	gst-libs/gst/rtsp/gstrtspextension.h \
	gst-libs/gst/rtsp/gstrtspmessage.h \
	gst-libs/gst/rtsp/gstrtsprange.h \
	gst-libs/gst/rtsp/gstrtsptransport.h \
	gst-libs/gst/rtsp/gstrtspurl.h
gst_rtsp_COPY_HEADERS_ANDROID := \
	gst-libs/gst/rtsp/gstrtsp-enumtypes.h

gst_rtsp_COPY_HEADERS := $(addprefix ../,$(gst_rtsp_COPY_HEADERS_BASE)) \
						 $(addprefix ../android/,$(gst_rtsp_COPY_HEADERS_ANDROID))	

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

rtsp_LOCAL_SRC_FILES_BASE:= \
	gst-libs/gst/rtsp/gstrtspbase64.c \
	gst-libs/gst/rtsp/gstrtspdefs.c \
	gst-libs/gst/rtsp/gstrtspconnection.c \
	gst-libs/gst/rtsp/gstrtspextension.c \
	gst-libs/gst/rtsp/gstrtspmessage.c \
	gst-libs/gst/rtsp/gstrtsprange.c \
	gst-libs/gst/rtsp/gstrtsptransport.c \
	gst-libs/gst/rtsp/gstrtspurl.c 
rtsp_LOCAL_SRC_FILES_ANDROID:= \
	gst-libs/gst/rtsp/gstrtsp-marshal.c \
	gst-libs/gst/rtsp/gstrtsp-enumtypes.c

LOCAL_SRC_FILES:= $(addprefix ../,$(rtsp_LOCAL_SRC_FILES_BASE)) \
				  $(addprefix ../android/,$(rtsp_LOCAL_SRC_FILES_ANDROID))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.11       \
    libgstbase-0.11         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0

LOCAL_MODULE:= libgstrtsp-0.11

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../gst-libs/gst/rtsp  \
    $(LOCAL_PATH)/../gst-libs           \
    $(LOCAL_PATH)/..            		\
    $(LOCAL_PATH)      			 		\
	$(LOCAL_PATH)/gst-libs/gst/rtsp     \
    $(TARGET_OUT_HEADERS)/gstreamer-0.11 \
	$(TARGET_OUT_HEADERS)/glib-2.0 		\
    $(TARGET_OUT_HEADERS)/glib-2.0/glib \
	external/libxml2/include

ifeq ($(STECONF_ANDROID_VERSION),"FROYO")
LOCAL_SHARED_LIBRARIES += libicuuc 
LOCAL_C_INCLUDES += external/icu4c/common
endif

LOCAL_CFLAGS := -DHAVE_CONFIG_H -DINET_ADDRSTRLEN=16 -DGSTREAMER_BUILT_FOR_ANDROID   
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false


LOCAL_COPY_HEADERS_TO := $(gst_rtsp_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_rtsp_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
