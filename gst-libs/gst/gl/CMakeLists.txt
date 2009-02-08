#FIXME: generate config.h
add_definitions(-DHAVE_CONFIG_H)

include_directories(AFTER
  .
  ${GLEW_INCLUDE_DIR}
  ${GSTREAMER_INCLUDE_DIR}
  ${GLIB2_INCLUDE_DIR})

if (NOT GST_USING_BINARY_REGISTRY)
include_directories(AFTER
  ${LIBXML2_INCLUDE_DIR}
  ${LIBICONV_INCLUDE_DIR})
endif (NOT GST_USING_BINARY_REGISTRY)

if (WIN32)

include_directories(AFTER
  ../../../win32/common)

add_library (gstgl STATIC
  gstglwindow_win32.c
  gstgldisplay.c
  gstglbuffer.c
  gstglfilter.c
  gstglshader.c)

target_link_libraries(gstgl
  general ${OPENGL_LIBRARIES}
  general ${GLEW_LIBRARY}
  general ${GLIB2_LIBRARIES}
  general ${GSTREAMER_LIBRARIES}
  gdi32
  winmm)

else (WIN32)

include_directories(AFTER
  ../../..)

add_library (gstgl STATIC
  gstglwindow_x11.c
  gstgldisplay.c
  gstglbuffer.c
  gstglfilter.c
  gstglshader.c)

target_link_libraries(gstgl
  general ${OPENGL_LIBRARIES}
  general ${GLEW_LIBRARY}
  general ${GLIB2_LIBRARIES}
  general ${GSTREAMER_LIBRARIES})
endif (WIN32)
