LOCAL_PATH:= $(call my-dir)
#----------------------------------------
# include 
gst_sdp_COPY_HEADERS_TO := gstreamer-0.11/gst/sdp
gst_sdp_COPY_HEADERS := \
	../gst-libs/gst/sdp/gstsdp.h \
	../gst-libs/gst/sdp/gstsdpmessage.h

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

sdp_LOCAL_SRC_FILES:= \
	gst-libs/gst/sdp/gstsdpmessage.c

LOCAL_SRC_FILES:= $(addprefix ../,$(sdp_LOCAL_SRC_FILES))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.11       \
    libgstbase-0.11         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0

LOCAL_MODULE:= libgstsdp-0.11

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../gst-libs/gst/sdp 	\
    $(LOCAL_PATH)/../gst-libs 			\
    $(LOCAL_PATH)/..         			\
    $(LOCAL_PATH)   	  				\
	$(LOCAL_PATH)/gst/tcp    			\
    $(TARGET_OUT_HEADERS)/gstreamer-0.11 \
	$(TARGET_OUT_HEADERS)/glib-2.0 		\
    $(TARGET_OUT_HEADERS)/glib-2.0/glib \
	external/libxml2/include 

ifeq ($(STECONF_ANDROID_VERSION),"FROYO")
LOCAL_SHARED_LIBRARIES += libicuuc 
LOCAL_C_INCLUDES += external/icu4c/common
endif

LOCAL_CFLAGS := -DHAVE_CONFIG_H  -DGSTREAMER_BUILT_FOR_ANDROID
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

LOCAL_COPY_HEADERS_TO := $(gst_sdp_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_sdp_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
