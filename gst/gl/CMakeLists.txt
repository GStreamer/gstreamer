add_definitions(-DHAVE_CONFIG_H)

if (PNG_FOUND AND ZLIB_FOUND)
add_definitions(-DHAVE_LIBPNG)
include_directories(AFTER 
  ${PNG_INCLUDE_DIR}
  ${ZLIB_INCLUDE_DIR})
endif (PNG_FOUND AND ZLIB_FOUND)

include_directories(AFTER
  ${GSTREAMER_INCLUDE_DIR}
  ${GLIB2_INCLUDE_DIR}
  ../../gst-libs/gst/gl
  ${GLEW_INCLUDE_DIR}
  ../../gst-libs
  ../..
  .
  effects)
  
if (NOT GST_USING_BINARY_REGISTRY)
include_directories(AFTER
  ${LIBXML2_INCLUDE_DIR}
  ${LIBICONV_INCLUDE_DIR})
endif (NOT GST_USING_BINARY_REGISTRY)

if (WIN32)
  include_directories(AFTER ../../win32/common)
endif (WIN32)

if (PNG_FOUND AND ZLIB_FOUND)
  set(OTHER_ELEMENTS
    gstgloverlay.c
    gstgldifferencematte.c
    gstglbumper.c)
endif (PNG_FOUND AND ZLIB_FOUND)

add_library (gstopengl SHARED
  gstopengl.c
  gstglimagesink.c
  gstglimagesink.h
  gstglupload.c
  gstgldownload.c
  gstglfilterblur.c
  gstglfiltercube.c
  gstglfilterlaplacian.c
  gstglfiltersobel.c
  gstglfilterglass.c
  gstglfilterapp.c
  gstglcolorscale.c
  gltestsrc.c
  gstgltestsrc.c
  ${OTHER_ELEMENTS}
  gstgleffects.c
  effects/gstgleffectssources.c
  effects/gstgleffectidentity.c
  effects/gstgleffectmirror.c
  effects/gstgleffectsqueeze.c
  effects/gstgleffectstretch.c
  effects/gstgleffecttunnel.c
  effects/gstgleffectfisheye.c
  effects/gstgleffecttwirl.c
  effects/gstgleffectbulge.c
  effects/gstgleffectsquare.c
  effects/gstgleffectlumatocurve.c
  effects/gstgleffectrgbtocurve.c
  effects/gstgleffectsin.c
  effects/gstgleffectglow.c
  effects/gstgleffectxray.c)
  
target_link_libraries(gstopengl
  general gstgl
  general ${PNG_LIBRARY}
  general ${ZLIB_LIBRARY})
