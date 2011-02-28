plugin_LTLIBRARIES = libgstmpegtsdemux.la

libgstmpegtsdemux_la_SOURCES = \
	gsttsdemux.c \
	gstmpegdesc.c \
	mpegtsbase.c	\
	mpegtspacketizer.c \
	mpegtsparse.c \
	tsdemux.c

libgstmpegtsdemux_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegtsdemux_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegtsdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegtsdemux_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstmpegdefs.h   \
	gstmpegdesc.h   \
	mpegtsbase.h	\
	mpegtspacketizer.h \
	mpegtsparse.h \
	tsdemux.h

