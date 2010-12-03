LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm
playbin_LOCAL_SRC_FILES_BASE:= \
	gst/playback/gstinputselector.c \
	gst/playback/gstplayback.c \
   	gst/playback/gstplaybin.c \
   	gst/playback/gstplaybin2.c \
   	gst/playback/gstplaysink.c \
   	gst/playback/gstplaybasebin.c \
	gst/playback/gstplay-enum.c \
	gst/playback/gstfactorylists.c \
   	gst/playback/gstscreenshot.c \
	gst/playback/gststreaminfo.c \
	gst/playback/gststreamselector.c \
	gst/playback/gstsubtitleoverlay.c
playbin_LOCAL_SRC_FILES_ANDROID:= \
	gst/playback/gstplay-marshal.c

LOCAL_SRC_FILES:= $(addprefix ../,$(playbin_LOCAL_SRC_FILES_BASE)) \
				  $(addprefix ../android/,$(playbin_LOCAL_SRC_FILES_ANDROID))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.10       \
    libgstbase-0.10         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 			\
	libgstpbutils-0.10 		\
	libgstinterfaces-0.10 	\
	libgstvideo-0.10

LOCAL_MODULE:= libgstplaybin

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../gst/playback  		\
    $(LOCAL_PATH)/../gst-libs      		\
    $(LOCAL_PATH)/..         			\
    $(LOCAL_PATH)   	  				\
	$(LOCAL_PATH)/gst/playback    		\
    $(TARGET_OUT_HEADERS)/gstreamer-0.10 \
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

#It's a gstreamer plugins, and it must be installed on ..../lib/gstreamer-0.10
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/gstreamer-0.10

include $(BUILD_SHARED_LIBRARY)
