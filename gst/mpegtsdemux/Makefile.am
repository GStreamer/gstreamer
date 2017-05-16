plugin_LTLIBRARIES = libgstmpegtsdemux.la

libgstmpegtsdemux_la_SOURCES = \
	mpegtspacketizer.c \
	mpegtsbase.c	\
	mpegtsparse.c \
	tsdemux.c	\
	gsttsdemux.c \
	pesparse.c

libgstmpegtsdemux_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegtsdemux_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/mpegts/libgstmpegts-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-$(GST_API_VERSION).la \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) \
	-lgstpbutils-@GST_API_VERSION@ -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgstmpegtsdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstmpegdefs.h   \
	gstmpegdesc.h   \
	mpegtsbase.h	\
	mpegtspacketizer.h \
	mpegtsparse.h \
	tsdemux.h	\
	pesparse.h
