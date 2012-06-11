# plugindir is set in configure

plugin_LTLIBRARIES = libgstresindvd.la

libgstresindvd_la_SOURCES = \
        plugin.c \
        resindvdbin.c \
	rsnaudiomunge.c \
	rsndec.c \
	rsnstreamselector.c \
        resindvdsrc.c  \
        gstmpegdesc.c   \
        gstmpegdemux.c  \
        gstpesfilter.c \
	rsnparsetter.c \
	rsnwrappedbuffer.c

libgstresindvd_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
$(GST_CFLAGS) $(DVDNAV_CFLAGS)
libgstresindvd_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
-lgstinterfaces-$(GST_API_VERSION) -lgstvideo-$(GST_API_VERSION) \
-lgstpbutils-$(GST_API_VERSION) \
$(GST_BASE_LIBS) $(GST_LIBS) $(DVDNAV_LIBS)
libgstresindvd_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstresindvd_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = resindvdbin.h \
	rsnaudiomunge.h \
	rsndec.h \
	rsnstreamselector.h \
        resindvdsrc.h \
        gstmpegdefs.h \
        gstmpegdesc.h \
        gstmpegdemux.h \
        gstpesfilter.h \
	rsnparsetter.h \
	rsnwrappedbuffer.h

EXTRA_DIST = resin-play resin-play2

