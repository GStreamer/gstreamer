plugin_LTLIBRARIES = libgstmpegdemux.la

libgstmpegdemux_la_SOURCES = \
	flumpegdemux.c \
	flutspatinfo.c \
	flutspmtinfo.c \
	flutspmtstreaminfo.c \
	gstmpegdemux.c \
	gstmpegdesc.c \
	gstmpegtsdemux.c \
	gstpesfilter.c \
	gstsectionfilter.c \
	mpegtsparse.c \
	mpegtspacketizer.c

libgstmpegdemux_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(LIBOIL_CFLAGS)
libgstmpegdemux_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBOIL_LIBS)
libgstmpegdemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegdemux_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstmpegdefs.h   \
	gstmpegdesc.h   \
	gstmpegdemux.h  \
	gstpesfilter.h  \
	gstmpegtsdemux.h \
	flutspatinfo.h \
	flutspmtinfo.h \
	flutspmtstreaminfo.h \
	gstsectionfilter.h \
	mpegtspacketizer.h \
	mpegtsparse.h

