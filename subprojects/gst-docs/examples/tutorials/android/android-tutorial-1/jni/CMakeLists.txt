cmake_minimum_required(VERSION 3.18.1)

project("android-tutorial-1" LANGUAGES C CXX)

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
set(GSTREAMER_PLUGINS         coreelements)
find_library(LOG_LIB log REQUIRED)
find_package(GStreamerMobile COMPONENTS ${GSTREAMER_PLUGINS} fonts REQUIRED)

add_library(tutorial-1 SHARED tutorial-1.c dummy.cpp)
target_link_libraries(tutorial-1
    PUBLIC
        GStreamer::mobile
        ${ANDROID_LIB}
        ${LOG_LIB}
)
set_target_properties(tutorial-1
    PROPERTIES
        C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden
)
