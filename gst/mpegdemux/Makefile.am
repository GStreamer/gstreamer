plugin_LTLIBRARIES = libgstmpegpsdemux.la

libgstmpegpsdemux_la_SOURCES = \
	plugin.c \
	gstmpegdemux.c \
	gstpesfilter.c 

libgstmpegpsdemux_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegpsdemux_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) -lgstbase-$(GST_API_VERSION) $(GST_LIBS)
libgstmpegpsdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegpsdemux_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstmpegdefs.h   \
	gstmpegdemux.h  \
	gstpesfilter.h
