LOCAL_PATH:= $(call my-dir)
#------------------------------------
#  include section 
#-----------------------------------
gst_controller_COPY_HEADERS_TO := gstreamer-0.11/gst/controller
gst_controller_COPY_HEADERS := \
		../libs/gst/controller/gstcontroller.h                 \
		../libs/gst/controller/gstcontrolsource.h              \
		../libs/gst/controller/gstinterpolationcontrolsource.h \
		../libs/gst/controller/gstlfocontrolsource.h   


include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

GST_MAJORMINOR:= 0.11

controller_LOCAL_SRC_FILES:= \
    libs/gst/controller/lib.c \
    libs/gst/controller/gstcontroller.c \
    libs/gst/controller/gstinterpolation.c \
    libs/gst/controller/gsthelper.c \
    libs/gst/controller/gstcontrolsource.c \
    libs/gst/controller/gstinterpolationcontrolsource.c \
    libs/gst/controller/gstlfocontrolsource.c
         
LOCAL_SRC_FILES:= $(addprefix ../,$(controller_LOCAL_SRC_FILES))
        	
LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.11     \
    libglib-2.0           \
    libgthread-2.0        \
    libgmodule-2.0        \
    libgobject-2.0

LOCAL_MODULE:= libgstcontroller-$(GST_MAJORMINOR)

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../libs 				\
    $(LOCAL_PATH)/..       				\
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

LOCAL_COPY_HEADERS_TO := $(gst_controller_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_controller_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
