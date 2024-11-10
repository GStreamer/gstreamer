cmake_minimum_required(VERSION 3.18.1)

project("android-vulkan-1" LANGUAGES C CXX)

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
set(GSTREAMER_PLUGINS         ${GSTREAMER_PLUGINS_CORE} ${GSTREAMER_PLUGINS_SYS} ${GSTREAMER_PLUGINS_EFFECTS} vulkan androidmedia)
set(GStreamer_EXTRA_DEPS gstreamer-video-1.0 gobject-2.0)
find_library(ANDROID_LIB android REQUIRED)
find_library(LOG_LIB log REQUIRED)
find_package(GStreamerMobile COMPONENTS ${GSTREAMER_PLUGINS} fonts ca_certificates REQUIRED)

add_library(vulkan-1 SHARED vulkan-1.c dummy.cpp)
target_link_libraries(vulkan-1
    PUBLIC
        GStreamer::mobile
        ${ANDROID_LIB}
        ${LOG_LIB}
)
set_target_properties(vulkan-1
    PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
)
