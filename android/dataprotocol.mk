LOCAL_PATH:= $(call my-dir)
#------------------------------------
#  include section 
#-----------------------------------
gst_dataprotocol_COPY_HEADERS_TO := gstreamer-0.10/gst/dataprotocol
gst_dataprotocol_COPY_HEADERS := \
		../libs/gst/dataprotocol/dataprotocol.h

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

GST_MAJORMINOR:= 0.10

dataprotocol_LOCAL_SRC_FILES:= \
    libs/gst/dataprotocol/dataprotocol.c
        
LOCAL_SRC_FILES:= $(addprefix ../,$(dataprotocol_LOCAL_SRC_FILES))
         
        	
LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.10       \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0

LOCAL_MODULE:= libgstdataprotocol-$(GST_MAJORMINOR)

LOCAL_TOP_PATH := $(LOCAL_PATH)/../../../..

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/..       				\
    $(LOCAL_PATH)/../libs 				\
    $(LOCAL_PATH)/../gst				\
	$(LOCAL_PATH)  						\
	$(LOCAL_PATH)/gst 					\
	$(LOCAL_PATH)/gst/parse				\
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

LOCAL_COPY_HEADERS_TO := $(gst_dataprotocol_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_dataprotocol_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
