LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := gstwebrtc
LOCAL_SRC_FILES := webrtc.c dummy.cpp

LOCAL_SHARED_LIBRARIES := gstreamer_android
LOCAL_LDLIBS := -llog -landroid
include $(BUILD_SHARED_LIBRARY)

ifndef GSTREAMER_ROOT_ANDROID
$(error GSTREAMER_ROOT_ANDROID is not defined!)
endif

ifeq ($(TARGET_ARCH_ABI),armeabi)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/arm
else ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/armv7
else ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/arm64
else ifeq ($(TARGET_ARCH_ABI),x86)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/x86
else ifeq ($(TARGET_ARCH_ABI),x86_64)
GSTREAMER_ROOT        := $(GSTREAMER_ROOT_ANDROID)/x86_64
else
$(error Target arch ABI not supported: $(TARGET_ARCH_ABI))
endif

GSTREAMER_NDK_BUILD_PATH  := $(GSTREAMER_ROOT)/share/gst-android/ndk-build/

include $(GSTREAMER_NDK_BUILD_PATH)/plugins.mk

GSTREAMER_PLUGINS_CORE_CUSTOM := coreelements app audioconvert audiorate audioresample videorate videoconvertscale videotestsrc audiotestsrc volume autodetect
GSTREAMER_PLUGINS_CODECS_CUSTOM := videoparsersbad vpx opus audioparsers opusparse androidmedia
GSTREAMER_PLUGINS_NET_CUSTOM := tcp rtsp rtp rtpmanager udp srtp webrtc dtls nice
GSTREAMER_PLUGINS         := $(GSTREAMER_PLUGINS_CORE_CUSTOM) $(GSTREAMER_PLUGINS_CODECS_CUSTOM) $(GSTREAMER_PLUGINS_NET_CUSTOM) \
                             $(GSTREAMER_PLUGINS_ENCODING)  \
                             $(GSTREAMER_PLUGINS_SYS) \
                             $(GSTREAMER_PLUGINS_PLAYBACK)

GSTREAMER_EXTRA_DEPS      := gstreamer-webrtc-1.0 gstreamer-sdp-1.0 gstreamer-video-1.0 libsoup-2.4 json-glib-1.0 glib-2.0
GSTREAMER_EXTRA_LIBS      := -liconv

G_IO_MODULES = openssl

include $(GSTREAMER_NDK_BUILD_PATH)/gstreamer-1.0.mk
