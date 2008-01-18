
plugin_LTLIBRARIES = libgstopengl.la

noinst_PROGRAMS = color_matrix

AM_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
AM_LIBS = $(GST_BASE_LIBS)

libgstopengl_la_SOURCES = \
	glimagesink.c \
	gstopengl.c \
	gstglupload.c \
	gstgldownload.c \
	gstgltestsrc.c \
	gstgltestsrc.h \
	gltestsrc.c \
	gltestsrc.h \
	gstglfilterexample.c \
	gstglconvert.c
# check order of CFLAGS and LIBS, shouldn't the order be the other way around
# (like in AM_CFLAGS)?
libgstopengl_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GL_CFLAGS) \
	-I$(top_srcdir)/gst-libs \
	-I$(top_srcdir)/gst-libs/gst/gl
libgstopengl_la_LIBADD = $(GL_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) -lgstinterfaces-$(GST_MAJORMINOR)
libgstopengl_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

