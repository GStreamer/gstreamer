LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

GST_MAJORMINOR:= 0.11

elements_LOCAL_SRC_FILES:= \
	plugins/elements/gstcapsfilter.c 	\
	plugins/elements/gstelements.c   	\
	plugins/elements/gstfakesrc.c  		\
	plugins/elements/gstfakesink.c 		\
	plugins/elements/gstfdsrc.c 		\
	plugins/elements/gstfdsink.c 		\
	plugins/elements/gstfilesink.c 		\
	plugins/elements/gstfilesrc.c 		\
	plugins/elements/gstidentity.c 		\
	plugins/elements/gstqueue.c 		\
	plugins/elements/gstqueue2.c 		\
	plugins/elements/gsttee.c 			\
	plugins/elements/gsttypefindelement.c \
	plugins/elements/gstmultiqueue.c

LOCAL_SRC_FILES:= $(addprefix ../,$(elements_LOCAL_SRC_FILES))


LOCAL_SHARED_LIBRARIES := \
    libgstbase-0.11       \
    libgstreamer-0.11     \
    libglib-2.0           \
    libgthread-2.0        \
    libgmodule-2.0        \
    libgobject-2.0

LOCAL_MODULE:= libgstcoreelements
#It's a gstreamer plugins, and it must be installed on ..../lib/gstreamer-0.11
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/gstreamer-0.11

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/..       				\
    $(LOCAL_PATH)/../libs 				\
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

include $(BUILD_SHARED_LIBRARY)
