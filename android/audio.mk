LOCAL_PATH:= $(call my-dir)
#----------------------------------------
# include 
gst_audio_COPY_HEADERS_TO := gstreamer-0.10/gst/audio
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
	gst-libs/gst/audio/multichannel.h \
	gst-libs/gst/audio/audio-enumtypes.h 

gst_audio_COPY_HEADERS := $(addprefix ../,$(gst_audio_COPY_HEADERS_BASE)) \

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
	gst-libs/gst/audio/gstringbuffer.c  \
	gst-libs/gst/audio/audio-enumtypes.c
        
LOCAL_SRC_FILES:= $(addprefix ../,$(audio_LOCAL_SRC_FILES_BASE))

LOCAL_SHARED_LIBRARIES := \
    libgstreamer-0.10       \
    libgstbase-0.10         \
    libglib-2.0             \
    libgthread-2.0          \
    libgmodule-2.0          \
    libgobject-2.0 			\
	libgstinterfaces-0.10

LOCAL_MODULE:= libgstaudio-0.10

LOCAL_CFLAGS := -DHAVE_CONFIG_H	-DGSTREAMER_BUILT_FOR_ANDROID \
	$(GST_PLUGINS_BASE_CFLAGS)
#
# define LOCAL_PRELINK_MODULE to false to not use pre-link map
#
LOCAL_PRELINK_MODULE := false

LOCAL_COPY_HEADERS_TO := $(gst_audio_COPY_HEADERS_TO)
LOCAL_COPY_HEADERS := $(gst_audio_COPY_HEADERS)
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
