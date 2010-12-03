LOCAL_PATH:= $(call my-dir)

gst_net_COPY_HEADERS_TO := gstreamer-0.10/gst/net
gst_net_COPY_HEADERS := \
		../libs/gst/net/gstnet.h 				\
		../libs/gst/net/gstnetclientclock.h 	\
		../libs/gst/net/gstnettimepacket.h 	\
		../libs/gst/net/gstnettimeprovider.h

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

GST_MAJORMINOR:= 0.10

net_LOCAL_SRC_FILES:= \
    libs/gst/net/gstnetclientclock.c \
    libs/gst/net/gstnettimepacket.c \
    libs/gst/net/gstnettimeprovider.c

LOCAL_SRC_FILES:= $(addprefix ../,$(net_LOCAL_SRC_FILES))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.10       \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0

LOCAL_MODULE:= libgstnet-$(GST_MAJORMINOR)

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../libs 				\
    $(LOCAL_PATH)/..       				\
    $(LOCAL_PATH)/../gst				\
    $(LOCAL_PATH)/ 						\
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

LOCAL_COPY_HEADERS_TO := $(gst_net_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_net_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
