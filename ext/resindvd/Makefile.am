# plugindir is set in configure

plugin_LTLIBRARIES = libresindvd.la

libresindvd_la_SOURCES = \
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

libresindvd_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
$(GST_CFLAGS) $(DVDNAV_CFLAGS)
libresindvd_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
-lgstinterfaces-$(GST_MAJORMINOR) -lgstvideo-$(GST_MAJORMINOR) \
-lgstpbutils-$(GST_MAJORMINOR) \
$(GST_BASE_LIBS) $(GST_LIBS) $(DVDNAV_LIBS)
libresindvd_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libresindvd_la_LIBTOOLFLAGS = --tag=disable-static

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

