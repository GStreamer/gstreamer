# Copyright (C) 2009 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := tutorial-1
LOCAL_SRC_FILES := tutorial-1.c
LOCAL_SHARED_LIBRARIES := gstreamer_android
LOCAL_LDLIBS := -landroid
include $(BUILD_SHARED_LIBRARY)

ifndef
GSTREAMER_SDK_ROOT        := $(CERBERO_PREFIX)
endif
GSTREAMER_NDK_BUILD_PATH  := $(GSTREAMER_SDK_ROOT)/share/gst-android/ndk-build/
GSTREAMER_PLUGINS         := coreelements ogg theora vorbis ffmpegcolorspace playback audioconvert audiorate audioresample coreindexers gio uridecodebin videorate videoscale typefindfunctions eglglessink soup amc matroska autodetect videofilter videotestsrc opensles
G_IO_MODULES              := gnutls
include $(GSTREAMER_NDK_BUILD_PATH)/gstreamer.mk
