LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

GST_MAJORMINOR:= 0.11

LOCAL_SRC_FILES:= ../tools/gst-launch.c       
         
LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.11       \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0

LOCAL_MODULE:= gst-launch


LOCAL_C_INCLUDES := \
    $(LOCAL_PATH) 						\
	$(LOCAL_PATH)/..                 	\
    $(LOCAL_PATH)/../tools           	\
    $(TARGET_OUT_HEADERS)/gstreamer-0.11 \
	$(TARGET_OUT_HEADERS)/glib-2.0 		\
    $(TARGET_OUT_HEADERS)/glib-2.0/glib \
	external/libxml2/include

ifeq ($(STECONF_ANDROID_VERSION),"FROYO")
LOCAL_SHARED_LIBRARIES += libicuuc 
LOCAL_C_INCLUDES += external/icu4c/common
endif

LOCAL_CFLAGS := \
	-DHAVE_CONFIG_H			

include $(BUILD_EXECUTABLE)
