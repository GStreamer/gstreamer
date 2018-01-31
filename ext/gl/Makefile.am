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
	$(GL_CFLAGS)

libgstopenglmixers_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstbadvideo-@GST_API_VERSION@.la \
	$(GST_GL_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_CONTROLLER_LIBS) \
	$(GST_LIBS) \
	$(GL_LIBS) \
	$(LIBM)

libgstopenglmixers_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstopenglmixers_la_LIBTOOLFLAGS = --tag=CC
