#FIXME: generate config.h
add_definitions(-DHAVE_CONFIG_H)

include_directories(AFTER
  ../../../win32/common
  ${GLEW_INCLUDE_DIR}
  ../freeglut
  ${GSTREAMER_INCLUDE_DIR}
  ${GLIB2_INCLUDE_DIR}
  ${LIBXML2_INCLUDE_DIR}
  ${LIBICONV_INCLUDE_DIR})

add_library (gstgl STATIC
  gstgldisplay.c
	gstglbuffer.c
	gstglfilter.c
	gstglshader.c)

#FIXME: get ride of this condition
if (WIN32)
target_link_libraries(gstgl
  general ${OPENGL_LIBRARIES}
  general ${GLEW_LIBRARY}
  general ${GLIB2_LIBRARIES}
  general ${GSTREAMER_LIBRARIES}
  gdi32
  winmm)
else (WIN32)
target_link_libraries(gstgl
  general ${OPENGL_LIBRARIES}
  general ${GLEW_LIBRARY}
  general ${GLIB2_LIBRARIES}
  general ${GSTREAMER_LIBRARIES})
endif (WIN32)
