LOCAL_PATH:= $(call my-dir)
#----------------------------------------
# include 
gst_interfaces_COPY_HEADERS_TO := gstreamer-0.11/gst/interfaces
gst_interfaces_COPY_HEADERS_BASE := \
	gst-libs/gst/interfaces/colorbalance.h \
	gst-libs/gst/interfaces/colorbalancechannel.h \
	gst-libs/gst/interfaces/mixer.h \
	gst-libs/gst/interfaces/mixeroptions.h \
	gst-libs/gst/interfaces/mixertrack.h \
	gst-libs/gst/interfaces/navigation.h \
	gst-libs/gst/interfaces/propertyprobe.h \
	gst-libs/gst/interfaces/streamvolume.h \
	gst-libs/gst/interfaces/tuner.h \
	gst-libs/gst/interfaces/tunerchannel.h \
	gst-libs/gst/interfaces/tunernorm.h \
	gst-libs/gst/interfaces/videoorientation.h \
	gst-libs/gst/interfaces/xoverlay.h
gst_interfaces_COPY_HEADERS_ANDROID := \
	gst-libs/gst/interfaces/interfaces-enumtypes.h 

gst_interfaces_COPY_HEADERS := $(addprefix ../,$(gst_interfaces_COPY_HEADERS_BASE)) \
						       $(addprefix ../android/,$(gst_interfaces_COPY_HEADERS_ANDROID))

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

interfaces_LOCAL_SRC_FILES_BASE:= \
   	gst-libs/gst/interfaces/colorbalance.c \
	gst-libs/gst/interfaces/colorbalancechannel.c \
   	gst-libs/gst/interfaces/mixer.c \
   	gst-libs/gst/interfaces/mixeroptions.c \
   	gst-libs/gst/interfaces/mixertrack.c \
   	gst-libs/gst/interfaces/navigation.c \
   	gst-libs/gst/interfaces/propertyprobe.c \
	gst-libs/gst/interfaces/streamvolume.c \
   	gst-libs/gst/interfaces/tuner.c \
   	gst-libs/gst/interfaces/tunernorm.c \
   	gst-libs/gst/interfaces/tunerchannel.c \
   	gst-libs/gst/interfaces/videoorientation.c \
   	gst-libs/gst/interfaces/xoverlay.c 
interfaces_LOCAL_SRC_FILES_ANDROID:= \
   	gst-libs/gst/interfaces/interfaces-marshal.c \
   	gst-libs/gst/interfaces/interfaces-enumtypes.c

LOCAL_SRC_FILES:= $(addprefix ../,$(interfaces_LOCAL_SRC_FILES_BASE)) \
				  $(addprefix ../android/,$(interfaces_LOCAL_SRC_FILES_ANDROID))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.11       \
    libgstbase-0.11         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0

LOCAL_MODULE:= libgstinterfaces-0.11

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../gst-libs/gst/interfaces \
    $(LOCAL_PATH)/../gst-libs           \
    $(LOCAL_PATH)/..            		\
    $(LOCAL_PATH)      			 		\
	$(LOCAL_PATH)/gst-libs/gst/interfaces \
    $(TARGET_OUT_HEADERS)/gstreamer-0.11 \
	$(TARGET_OUT_HEADERS)/glib-2.0 		\
    $(TARGET_OUT_HEADERS)/glib-2.0/glib \
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

LOCAL_COPY_HEADERS_TO := $(gst_interfaces_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_interfaces_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
