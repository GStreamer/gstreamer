LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

rtpmanager_LOCAL_SRC_FILES_BASE:= \
	gst/rtpmanager/gstrtpmanager.c \
	gst/rtpmanager/gstrtpbin.c \
	gst/rtpmanager/gstrtpjitterbuffer.c \
	gst/rtpmanager/gstrtpptdemux.c \
	gst/rtpmanager/gstrtpssrcdemux.c \
	gst/rtpmanager/rtpjitterbuffer.c \
	gst/rtpmanager/rtpsession.c \
	gst/rtpmanager/rtpsource.c \
	gst/rtpmanager/rtpstats.c \
	gst/rtpmanager/gstrtpsession.c	
rtpmanager_LOCAL_SRC_FILES_ANDROID:= \
	gst/rtpmanager/gstrtpbin-marshal.c
 
LOCAL_SRC_FILES:= $(addprefix ../,$(rtpmanager_LOCAL_SRC_FILES_BASE)) \
				  $(addprefix ../android/,$(rtpmanager_LOCAL_SRC_FILES_ANDROID))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.10       \
    libgstbase-0.10         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 			\
	libgstnetbuffer-0.10    \
	libgstrtp-0.10

LOCAL_MODULE:= libgstrtpmanager

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../gst/rtpmanager 	\
    $(LOCAL_PATH)/..         	  		\
    $(LOCAL_PATH)/../gst-libs       	\
	$(LOCAL_PATH)          				\
	$(LOCAL_PATH)/gst/rtpmanager 		\
	$(TARGET_OUT_HEADERS)/gstreamer-0.10 \
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

#It's a gstreamer plugins, and it must be installed on ..../lib/gstreamer-0.10
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/gstreamer-0.10

include $(BUILD_SHARED_LIBRARY)
