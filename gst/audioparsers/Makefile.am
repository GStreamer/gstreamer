plugin_LTLIBRARIES = libgstaudioparsers.la

libgstaudioparsers_la_SOURCES = \
	gstaacparse.c gstamrparse.c gstac3parse.c \
	gstdcaparse.c gstflacparse.c gstmpegaudioparse.c \
	plugin.c

libgstaudioparsers_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstaudioparsers_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR) \
	-lgstaudio-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstaudioparsers_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudioparsers_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstaacparse.h gstamrparse.h gstac3parse.h \
	gstdcaparse.h gstflacparse.h gstmpegaudioparse.h
