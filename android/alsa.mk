LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

alsa_LOCAL_SRC_FILES:= \
	ext/alsa/gstalsadeviceprobe.c \
   	ext/alsa/gstalsamixer.c \
   	ext/alsa/gstalsamixerelement.c \
   	ext/alsa/gstalsamixertrack.c \
   	ext/alsa/gstalsamixeroptions.c \
   	ext/alsa/gstalsaplugin.c \
   	ext/alsa/gstalsasink.c \
   	ext/alsa/gstalsasrc.c \
   	ext/alsa/gstalsa.c 

LOCAL_SRC_FILES:= $(addprefix ../,$(alsa_LOCAL_SRC_FILES))

LOCAL_SHARED_LIBRARIES := \
	libdl                   \
    libgstreamer-0.10       \
    libgstbase-0.10         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 			\
	libgstinterfaces-0.10   \
	libgstaudio-0.10

LOCAL_MODULE:= libgstalsa

LOCAL_WHOLE_STATIC_LIBRARIES := libasound

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../ext/alsa 			\
    $(LOCAL_PATH)/../gst-libs      		\
    $(LOCAL_PATH)/..         			\
    $(LOCAL_PATH)   	  				\
    $(TARGET_OUT_HEADERS)/gstreamer-0.10 \
	$(TARGET_OUT_HEADERS)/glib-2.0 		\
    $(TARGET_OUT_HEADERS)/glib-2.0/glib \
	external/libxml2/include 			\
    external/alsa-lib/include      

ifeq ($(STECONF_ANDROID_VERSION),"FROYO")
LOCAL_SHARED_LIBRARIES += libicuuc 
LOCAL_C_INCLUDES += external/icu4c/common
endif
LOCAL_CFLAGS := -DHAVE_CONFIG_H	-DGSTREAMER_BUILT_FOR_ANDROID
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

#It's a gstreamer plugins, and it must be installed on ..../lib/gstreamer-0.10
LOCAL_MODULE_PATH := $(TARGET_OUT)/lib/gstreamer-0.10


$(TARGET_OUT)/lib/libgstalsa.so:

ALL_PREBUILT += $(TARGET_OUT)/lib/libgstalsa.so

include $(BUILD_SHARED_LIBRARY)
