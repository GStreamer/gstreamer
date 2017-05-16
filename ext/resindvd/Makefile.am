# plugindir is set in configure

plugin_LTLIBRARIES = libgstresindvd.la

libgstresindvd_la_SOURCES = \
        plugin.c \
        resindvdbin.c \
        resindvdsrc.c  \
	rsndec.c \
        gstmpegdesc.c   \
        gstmpegdemux.c  \
        gstpesfilter.c \
	rsninputselector.c \
	rsnparsetter.c

libgstresindvd_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
$(GST_CFLAGS) $(DVDNAV_CFLAGS)
libgstresindvd_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
-lgstvideo-$(GST_API_VERSION) -lgstpbutils-$(GST_API_VERSION) \
-lgsttag-$(GST_API_VERSION) \
$(GST_BASE_LIBS) $(GST_LIBS) $(GMODULE_NO_EXPORT_LIBS) $(DVDNAV_LIBS)
libgstresindvd_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = resindvdbin.h \
	rsndec.h \
	rsninputselector.h \
        resindvdsrc.h \
        gstmpegdefs.h \
        gstmpegdesc.h \
        gstmpegdemux.h \
        gstpesfilter.h \
	rsnparsetter.h

EXTRA_DIST = resin-play resin-play2

