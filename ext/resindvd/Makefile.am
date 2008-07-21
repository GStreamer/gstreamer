# plugindir is set in configure

plugin_LTLIBRARIES = libresindvd.la

libresindvd_la_SOURCES = \
        plugin.c \
        resindvdbin.c \
	rsnaudiomunge.c \
	rsnbasesrc.c \
	rsnpushsrc.c \
	rsnstreamselector.c \
        resindvdsrc.c  \
        gstmpegdesc.c   \
        gstmpegdemux.c  \
        gstpesfilter.c \
	rsnparsetter.c \
	rsnwrappedbuffer.c

libresindvd_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(DVDNAV_CFLAGS)
libresindvd_la_LIBADD = $(GST_PLUGINS_BASE_CFLAGS) -lgstvideo-$(GST_MAJORMINOR) $(GST_BASE_LIBS) $(GST_LIBS) $(DVDNAV_LIBS)
libresindvd_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = resindvdbin.h \
	rsnaudiomunge.h \
	rsnbasesrc.h \
	rsnpushsrc.h \
	rsnstreamselector.h \
        resindvdsrc.h \
        gstmpegdefs.h \
        gstmpegdesc.h \
        gstmpegdemux.h \
        gstpesfilter.h \
	rsnparsetter.h \
	rsnwrappedbuffer.h

