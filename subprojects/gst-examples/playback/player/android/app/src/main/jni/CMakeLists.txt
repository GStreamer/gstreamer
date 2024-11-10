cmake_minimum_required(VERSION 3.18.1)

project("android-player" LANGUAGES C CXX)

if(NOT DEFINED GSTREAMER_ROOT_ANDROID)
    message(FATAL_ERROR "GSTREAMER_ROOT_ANDROID is not defined!")
endif()

if(ANDROID_ABI STREQUAL "armeabi")
    set(GSTREAMER_ROOT "${GSTREAMER_ROOT_ANDROID}/arm")
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(GSTREAMER_ROOT "${GSTREAMER_ROOT_ANDROID}/armv7")
elseif(ANDROID_ABI STREQUAL "arm64-v8a")
    set(GSTREAMER_ROOT "${GSTREAMER_ROOT_ANDROID}/arm64")
elseif(ANDROID_ABI STREQUAL "x86")
    set(GSTREAMER_ROOT "${GSTREAMER_ROOT_ANDROID}/x86")
elseif(ANDROID_ABI STREQUAL "x86_64")
    set(GSTREAMER_ROOT "${GSTREAMER_ROOT_ANDROID}/x86_64")
else()
    message(FATAL_ERROR "Target arch ABI not supported: ${ANDROID_ABI}")
endif()

list(APPEND CMAKE_MODULE_PATH "${GSTREAMER_ROOT}/share/cmake")

set(GSTREAMER_NDK_BUILD_PATH  "${GSTREAMER_ROOT}/share/gst-android/ndk-build/")
include("${GSTREAMER_NDK_BUILD_PATH}/plugins.cmake")
set(GSTREAMER_PLUGINS         ${GSTREAMER_PLUGINS_CORE} ${GSTREAMER_PLUGINS_PLAYBACK} ${GSTREAMER_PLUGINS_CODECS} ${GSTREAMER_PLUGINS_NET} ${GSTREAMER_PLUGINS_SYS} ${GSTREAMER_PLUGINS_CODECS_RESTRICTED} ${GSTREAMER_CODECS_GPL} ${GSTREAMER_PLUGINS_ENCODING} ${GSTREAMER_PLUGINS_VIS} ${GSTREAMER_PLUGINS_EFFECTS} ${GSTREAMER_PLUGINS_NET_RESTRICTED})
set(GStreamer_EXTRA_DEPS gstreamer-play-1.0 gstreamer-player-1.0 gstreamer-video-1.0 glib-2.0)
find_library(ANDROID_LIB android REQUIRED)
find_library(LOG_LIB log REQUIRED)

find_package(GStreamerMobile COMPONENTS ${GSTREAMER_PLUGINS} fonts REQUIRED)

add_library(gstplayer SHARED player.c dummy.cpp)
target_link_libraries(gstplayer
    PUBLIC
        GStreamer::mobile
        ${ANDROID_LIB}
        ${LOG_LIB}
)
set_target_properties(gstplayer
    PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
)
