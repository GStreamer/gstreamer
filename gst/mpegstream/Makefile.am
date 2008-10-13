plugin_LTLIBRARIES = libgstmpegstream.la

libgstmpegstream_la_SOURCES = gstmpegstream.c \
                              gstmpegparse.c \
                              gstmpegdemux.c \
                              gstdvddemux.c \
                              gstmpegpacketize.c \
                              gstmpegclock.c
# gstrfc2250enc.c
libgstmpegstream_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstmpegstream_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_MAJORMINOR@
libgstmpegstream_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegstream_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstmpegparse.h \
                 gstmpegdemux.h \
                 gstdvddemux.h \
                 gstmpegpacketize.h \
                 gstmpegclock.h \
		 gstrfc2250enc.h

EXTRA_DIST = README notes
