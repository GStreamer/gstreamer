LOCAL_PATH:= $(call my-dir)
#----------------------------------------
# include 
gst_netbuffer_COPY_HEADERS_TO := gstreamer-0.11/gst/netbuffer
gst_netbuffer_COPY_HEADERS := \
	../gst-libs/gst/netbuffer/gstnetbuffer.h

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

netbuffer_LOCAL_SRC_FILES:= \
	gst-libs/gst/netbuffer/gstnetbuffer.c

LOCAL_SRC_FILES:= $(addprefix ../,$(netbuffer_LOCAL_SRC_FILES))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.11       \
    libgstbase-0.11         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0

LOCAL_MODULE:= libgstnetbuffer-0.11

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../gst-libs/gst/netbuffer \
    $(LOCAL_PATH)/../gst-libs 				\
    $(LOCAL_PATH)/..         				\
    $(LOCAL_PATH)   	  					\
	$(LOCAL_PATH)/../gst/tcp    			\
    $(TARGET_OUT_HEADERS)/gstreamer-0.11    \
	$(TARGET_OUT_HEADERS)/glib-2.0 			\
    $(TARGET_OUT_HEADERS)/glib-2.0/glib     \
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

LOCAL_COPY_HEADERS_TO := $(gst_netbuffer_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_netbuffer_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
