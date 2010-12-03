LOCAL_PATH:= $(call my-dir)

#------------------------------------
#  include section 
#-----------------------------------
gst_base_COPY_HEADERS_TO := gstreamer-0.10/gst/base
gst_base_COPY_HEADERS := \
		../libs/gst/base/gstadapter.h         \
		../libs/gst/base/gstbasesink.h        \
		../libs/gst/base/gstbasesrc.h         \
		../libs/gst/base/gstbasetransform.h   \
		../libs/gst/base/gstbitreader.h       \
		../libs/gst/base/gstbytereader.h      \
		../libs/gst/base/gstbytewriter.h      \
		../libs/gst/base/gstcollectpads.h     \
		../libs/gst/base/gstdataqueue.h       \
		../libs/gst/base/gstpushsrc.h         \
		../libs/gst/base/gsttypefindhelper.h

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

GST_MAJORMINOR:= 0.10

base_LOCAL_SRC_FILES:= \
    libs/gst/base/gstadapter.c          \
    libs/gst/base/gstbasesink.c 		\
    libs/gst/base/gstbasesrc.c			\
    libs/gst/base/gstbasetransform.c 	\
    libs/gst/base/gstbitreader.c 		\
    libs/gst/base/gstbytereader.c 		\
    libs/gst/base/gstbytewriter.c 		\
    libs/gst/base/gstcollectpads.c 		\
    libs/gst/base/gstpushsrc.c 			\
    libs/gst/base/gsttypefindhelper.c 	\
    libs/gst/base/gstdataqueue.c 	

LOCAL_SRC_FILES:= $(addprefix ../,$(base_LOCAL_SRC_FILES))
         
        	
LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.10       \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0

LOCAL_MODULE:= libgstbase-$(GST_MAJORMINOR)


LOCAL_C_INCLUDES := \
   	$(LOCAL_PATH)  						\
	$(LOCAL_PATH)/gst 					\
    $(LOCAL_PATH)/../libs/gst/base   	\
    $(LOCAL_PATH)/..       				\
    $(LOCAL_PATH)/../gst				\
    $(LOCAL_PATH)/../libs 				\
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

LOCAL_COPY_HEADERS_TO := $(gst_base_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_base_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
