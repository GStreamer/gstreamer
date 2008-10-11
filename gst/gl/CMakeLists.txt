add_definitions(-DHAVE_CONFIG_H -DHAVE_GDKPIXBUF)

include_directories(AFTER
  ../../win32/common
  ${GSTREAMER_INCLUDE_DIR}
  ${GLIB2_INCLUDE_DIR}
  ${LIBXML2_INCLUDE_DIR}
  ${LIBICONV_INCLUDE_DIR}
  ${LIBATK_INCLUDE_DIR}
  ${CAIRO_INCLUDE_DIR}
  ${GTK2_INCLUDE_DIR}
  ../../gst-libs/gst/gl
  ${GLEW_INCLUDE_DIR}
  ../../gst-libs/gst/freeglut
  ../../gst-libs
  .
  effects)

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
	effects/gstgleffectglow.c)
  
target_link_libraries(gstopengl
  general gstgl
  general ${GTK2_LIBRARIES})
