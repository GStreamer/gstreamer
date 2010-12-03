LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

GST_MAJORMINOR:= 0.10

indexers_LOCAL_SRC_FILES:= \
    plugins/indexers/gstindexers.c \
	plugins/indexers/gstmemindex.c \
	plugins/indexers/gstfileindex.c
    
LOCAL_SRC_FILES:= $(addprefix ../,$(indexers_LOCAL_SRC_FILES))

LOCAL_STATIC_LIBRARIES := \
		libxml2   	

LOCAL_SHARED_LIBRARIES := \
    libgstbase-0.10       \
    libgstreamer-0.10       \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0

LOCAL_MODULE:= libgstcoreindexers
#It's a gstreamer plugins, and it must be installed on ..../lib/gstreamer-0.10
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/gstreamer-0.10

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


include $(BUILD_SHARED_LIBRARY)
