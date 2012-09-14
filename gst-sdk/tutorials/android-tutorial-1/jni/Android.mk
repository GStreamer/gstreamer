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

GSTREAMER_PLUGINS = coreelements audiotestsrc videotestsrc ogg theora vorbis ffmpegcolorspace playback app audioconvert audiorate audioresample adder coreindexers gdp gio uridecodebin videorate videoscale typefindfunctions libvisual pango subparse eglglessink soup amc matroska
GSTREAMER_STATIC_PLUGINS_PATH=/home/fluendo/cerbero/dist/android_arm/lib/gstreamer-0.10/static
GSTREAMER_MK_PATH=/home/fluendo/cerbero/data/ndk-build/
include $(GSTREAMER_MK_PATH)/gstreamer.mk

