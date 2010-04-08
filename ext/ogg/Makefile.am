plugin_LTLIBRARIES = libgstogg.la

libgstogg_la_SOURCES = \
	gstogg.c \
	gstoggdemux.c \
	gstoggmux.c \
	gstogmparse.c \
	gstoggaviparse.c \
	gstoggparse.c \
	gstoggstream.c \
	dirac_parse.c \
	vorbis_parse.c

noinst_HEADERS = \
	gstogg.h \
	gstoggdemux.h \
	gstoggmux.h \
	gstoggstream.h \
	dirac_parse.h \
	vorbis_parse.h

libgstogg_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(OGG_CFLAGS)
libgstogg_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/riff/libgstriff-$(GST_MAJORMINOR).la \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-$(GST_MAJORMINOR).la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(OGG_LIBS)
libgstogg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstogg_la_LIBTOOLFLAGS = --tag=disable-static

