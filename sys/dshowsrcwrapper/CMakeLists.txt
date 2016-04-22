
CMAKE_MINIMUM_REQUIRED (VERSION 2.8)

PROJECT(dshowsrcwrapper)

SET(GST_INSTALL_BASE "C:\\gstreamer\\1.0\\x86" CACHE PATH "Path to the GStreamer install base")
SET(SDK_INSTALL_BASE "C:\\Program Files\\Microsoft SDKs\\Windows\\v7.1" CACHE PATH "Path to the Windows SDK root")

SET_PROPERTY(
  DIRECTORY
  APPEND PROPERTY COMPILE_DEFINITIONS
  HAVE_CONFIG_H
)

INCLUDE_DIRECTORIES(
  ${GST_INSTALL_BASE}/include
  ${GST_INSTALL_BASE}/include/gstreamer-1.0
  ${GST_INSTALL_BASE}/include/glib-2.0
  ${GST_INSTALL_BASE}/lib/glib-2.0/include
  ${GST_INSTALL_BASE}/lib/gstreamer-1.0/include
  ${SDK_INSTALL_BASE}/Samples/multimedia/directshow/baseclasses
  ${PROJECT_SOURCE_DIR}/../../win32/common
)

LINK_DIRECTORIES(
  ${GST_INSTALL_BASE}/lib
  ${SDK_INSTALL_BASE}/Samples/multimedia/directshow/baseclasses/Release_MBCS
  ${SDK_INSTALL_BASE}/Samples/multimedia/directshow/baseclasses/Debug_MBCS
)

SET(CMAKE_SHARED_LINKER_FLAGS "${CMALE_SHARED_LINKER_FLAGS} /SAFESEH:NO")

ADD_LIBRARY(libgstdshowsrcwrapper SHARED
  gstdshow.cpp
  gstdshow.h
  gstdshowfakesink.cpp
  gstdshowfakesink.h
  gstdshowaudiosrc.cpp
  gstdshowaudiosrc.h
  gstdshowvideosrc.cpp
  gstdshowvideosrc.h
  gstdshowsrcwrapper.cpp
)

TARGET_LINK_LIBRARIES(libgstdshowsrcwrapper
  gstreamer-1.0
  gstaudio-1.0
  gstvideo-1.0
  gstbase-1.0
  glib-2.0
  gobject-2.0
  debug strmbasd
  optimized strmbase
  rpcrt4
  uuid
  winmm
)
