plugin_LTLIBRARIES = libgstaudioparsersbad.la

libgstaudioparsersbad_la_SOURCES = \
	gstaacparse.c gstamrparse.c gstac3parse.c \
	gstdcaparse.c gstflacparse.c gstmpegaudioparse.c \
	plugin.c

libgstaudioparsersbad_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstaudioparsersbad_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/baseparse/libgstbaseparse-$(GST_MAJORMINOR).la \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR) \
	-lgstaudio-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstaudioparsersbad_la_LDFLAGS = $(PACKAGE_LIBS) $(GST_PLUGIN_LDFLAGS)
libgstaudioparsersbad_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstaacparse.h gstamrparse.h gstac3parse.h \
	gstdcaparse.h gstflacparse.h gstmpegaudioparse.h
