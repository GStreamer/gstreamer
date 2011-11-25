add_definitions(-DHAVE_CONFIG_H)

include_directories(AFTER
  ${GSTREAMER_INCLUDE_DIR}
  ${GLIB2_INCLUDE_DIR}
  ../../gst-libs/gst/gl
  ../../gst-libs
  ../..
  .)

add_library (gstlibvisualgl SHARED
  visual-gl.c)

target_link_libraries(gstlibvisualgl
  general gstgl)
