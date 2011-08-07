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
	gst-libs/gst/interfaces/videooverlay.h \
	gst-libs/gst/interfaces/interfaces-enumtypes.h 

gst_interfaces_COPY_HEADERS := $(addprefix ../,$(gst_interfaces_COPY_HEADERS_BASE))

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
   	gst-libs/gst/interfaces/videooverlay.c \
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

LOCAL_CFLAGS := -DHAVE_CONFIG_H	-DGSTREAMER_BUILT_FOR_ANDROID \
	$(GST_PLUGINS_BASE_CFLAGS)
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

LOCAL_COPY_HEADERS_TO := $(gst_interfaces_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_interfaces_COPY_HEADERS)

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
