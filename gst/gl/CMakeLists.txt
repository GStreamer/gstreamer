add_definitions(-DHAVE_CONFIG_H)

if (PNG_FOUND)
add_definitions(-DHAVE_LIBPNG)
endif (PNG_FOUND)

include_directories(AFTER
  ${GSTREAMER_INCLUDE_DIR}
  ${GLIB2_INCLUDE_DIR}
  ${LIBXML2_INCLUDE_DIR}
  ${LIBICONV_INCLUDE_DIR}
  ${PNG_INCLUDE_DIR}
  ../../gst-libs/gst/gl
  ${GLEW_INCLUDE_DIR}
  ../../gst-libs
  ../..
  .
  effects)

if (WIN32)
  include_directories(AFTER ../../win32/common)
endif (WIN32)

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
  gstgleffects.c
  gstglpixbufoverlay.c
  gstgldifferencematte.c
  gstglbumper.c
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
  general ${PNG_LIBRARY})
