LOCAL_PATH:= $(call my-dir)
#----------------------------------------
# include 
gst_audio_COPY_HEADERS_TO := gstreamer-0.11/gst/audio
gst_audio_COPY_HEADERS_BASE := \
	gst-libs/gst/audio/audio.h \
	gst-libs/gst/audio/gstaudioclock.h \
	gst-libs/gst/audio/gstaudiofilter.h \
	gst-libs/gst/audio/gstaudiosink.h \
	gst-libs/gst/audio/gstaudiosrc.h \
	gst-libs/gst/audio/gstbaseaudiosink.h \
	gst-libs/gst/audio/gstbaseaudiosrc.h \
	gst-libs/gst/audio/gstringbuffer.h \
	gst-libs/gst/audio/mixerutils.h \
	gst-libs/gst/audio/multichannel.h

gst_audio_COPY_HEADERS_ANDROID := \
	gst-libs/gst/audio/audio-enumtypes.h 

gst_audio_COPY_HEADERS := $(addprefix ../,$(gst_audio_COPY_HEADERS_BASE)) \
	          			  $(addprefix ../android/,$(gst_audio_COPY_HEADERS_ANDROID))

include $(CLEAR_VARS)

LOCAL_ARM_MODE := arm

# TODO: mixerutils.c is removed, because it depends on gst-lib/gst/interface.
# We need add it later
audio_LOCAL_SRC_FILES_BASE:= \
	gst-libs/gst/audio/audio.c \
	gst-libs/gst/audio/gstaudioclock.c \
	gst-libs/gst/audio/mixerutils.c \
	gst-libs/gst/audio/multichannel.c \
	gst-libs/gst/audio/gstaudiofilter.c \
	gst-libs/gst/audio/gstaudiosink.c \
	gst-libs/gst/audio/gstaudiosrc.c \
	gst-libs/gst/audio/gstbaseaudiosink.c \
	gst-libs/gst/audio/gstbaseaudiosrc.c \
	gst-libs/gst/audio/gstringbuffer.c 
audio_LOCAL_SRC_FILES_ANDROID:= \
	gst-libs/gst/audio/audio-enumtypes.c
        
LOCAL_SRC_FILES:= $(addprefix ../,$(audio_LOCAL_SRC_FILES_BASE)) \
				  $(addprefix ../android/,$(audio_LOCAL_SRC_FILES_ANDROID))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.11       \
    libgstbase-0.11         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 			\
	libgstinterfaces-0.11

LOCAL_MODULE:= libgstaudio-0.11

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/../gst-libs/gst/audio \
    $(LOCAL_PATH)/../gst-libs           \
    $(LOCAL_PATH)/..            		\
    $(LOCAL_PATH)      			 		\
	$(LOCAL_PATH)/gst-libs/gst/audio   	\
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

LOCAL_COPY_HEADERS_TO := $(gst_audio_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_audio_COPY_HEADERS)

include $(BUILD_SHARED_LIBRARY)
