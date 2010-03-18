plugin_LTLIBRARIES = libgstavi.la

libgstavi_la_SOURCES = \
	gstavi.c \
	gstavimux.c \
	gstavidemux.c \
	gstavisubtitle.c

noinst_HEADERS = \
	avi-ids.h \
	gstavimux.h \
	gstavidemux.h \
	gstavisubtitle.h

libgstavi_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstavi_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	-lgstriff-@GST_MAJORMINOR@ \
	-lgstaudio-@GST_MAJORMINOR@ \
	-lgsttag-@GST_MAJORMINOR@

libgstavi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstavi_la_LIBTOOLFLAGS = --tag=disable-static

EXTRA_DIST = README
