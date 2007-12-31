
plugin_LTLIBRARIES = libgstglimagesink.la

noinst_PROGRAMS = color_matrix

AM_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS)
AM_LIBS = $(GST_BASE_LIBS)

libgstglimagesink_la_SOURCES = \
	glimagesink.c \
	gstgldisplay.c \
	gstopengl.c \
	glextensions.c \
	gstglbuffer.c \
	gstglupload.c \
	gstgldownload.c \
	gstgltestsrc.c \
	gltestsrc.c \
	gstglfilter.c \
	gstglfilterexample.c \
	gstglconvert.c
libgstglimagesink_la_CFLAGS = $(GST_CFLAGS) $(X_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GL_CFLAGS)
libgstglimagesink_la_LIBADD = $(GL_LIBS) \
	$(GST_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-$(GST_MAJORMINOR)
libgstglimagesink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	glimagesink.h \
	gstgldisplay.h \
	glextensions.h \
	gstgltestsrc.h \
	gltestsrc.h \
	gstglbuffer.h \
	gstglfilter.h
