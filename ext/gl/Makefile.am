plugin_LTLIBRARIES = libgstopenglmixers.la

# These have to stay in -bad until we can move GstVideoAggregator to -base
libgstopenglmixers_la_SOURCES = \
	gstopengl.c \
	gstglbasemixer.c \
	gstglmixer.c \
	gstglmixerbin.c \
	gstglstereomix.c \
	gstglutils.c \
	gstglvideomixer.c

noinst_HEADERS = \
	gstglbasemixer.h \
	gstglmixer.h \
	gstglmixerbin.h \
	gstglstereomix.h \
	gstglutils.h \
	gstglvideomixer.h

# full opengl required
if USE_OPENGL
libgstopenglmixers_la_SOURCES += gstglmosaic.c
noinst_HEADERS += gstglmosaic.h
endif

# FIXME: can we remove GL flags here?

# check order of CFLAGS and LIBS, shouldn't the order be the other way around
# (like in AM_CFLAGS)?
libgstopenglmixers_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CONTROLLER_CFLAGS) \
	$(GST_CFLAGS) \
	$(X11_CFLAGS) \
	-Dgst_gl_context_gen_shader=gst_gl_bad_context_gen_shader \
	-Dgst_gl_multiply_matrix4=Dgst_gl_bad_multiply_matrix4 \
	-Dgst_gl_get_affine_transformation_meta_as_ndc_ext=Dgst_gl_bad_get_affine_transformation_meta_as_ndc_ext \
	-Dgst_gl_set_affine_transformation_meta_from_ndc_ext=Dgst_gl_bad_set_affine_transformation_meta_from_ndc_ext

libgstopenglmixers_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstbadvideo-@GST_API_VERSION@.la \
	$(GST_GL_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_CONTROLLER_LIBS) \
	$(GST_LIBS) \
	$(X11_LIBS) \
	$(LIBM)

libgstopenglmixers_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstopenglmixers_la_LIBTOOLFLAGS = --tag=CC
