LOCAL_PATH:= $(call my-dir)
#----------------------------------------
# include 
gst_app_COPY_HEADERS_TO := gstreamer-0.11/gst/app
gst_app_COPY_HEADERS := \
	../gst-libs/gst/app/gstappbuffer.h \
	../gst-libs/gst/app/gstappsink.h \
	../gst-libs/gst/app/gstappsrc.h

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

app_LOCAL_SRC_FILES_BASE:= \
	gst-libs/gst/app/gstappsrc.c \
	gst-libs/gst/app/gstappbuffer.c \
	gst-libs/gst/app/gstappsink.c 
app_LOCAL_SRC_FILES_ANDROID:= \
	gst-libs/gst/app/gstapp-marshal.c
  	
LOCAL_SRC_FILES:= $(addprefix ../,$(app_LOCAL_SRC_FILES_BASE)) \
				  $(addprefix ../android/,$(app_LOCAL_SRC_FILES_ANDROID))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.11       \
    libgstbase-0.11         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 

LOCAL_MODULE:= libgstapp-0.11

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../gst-libs/gst/app   \
    $(LOCAL_PATH)/../gst-libs      		\
    $(LOCAL_PATH)/..         			\
    $(LOCAL_PATH)   	  				\
	$(LOCAL_PATH)/gst-libs/gst/app 		\
    $(TARGET_OUT_HEADERS)/gstreamer-0.11 \
	$(TARGET_OUT_HEADERS)/glib-2.0 		\
    $(TARGET_OUT_HEADERS)/glib-2.0/glib \
	external/libxml2/include

ifeq ($(STECONF_ANDROID_VERSION),"FROYO")
LOCAL_SHARED_LIBRARIES += libicuuc 
LOCAL_C_INCLUDES += external/icu4c/common
endif

LOCAL_CFLAGS := -DHAVE_CONFIG_H	 -DGSTREAMER_BUILT_FOR_ANDROID
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

LOCAL_COPY_HEADERS_TO := $(gst_app_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_app_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
