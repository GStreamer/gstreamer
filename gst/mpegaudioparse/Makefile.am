plugin_LTLIBRARIES = libgstmpegaudioparse.la

libgstmpegaudioparse_la_SOURCES = plugin.c gstmpegaudioparse.c gstxingmux.c
libgstmpegaudioparse_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstmpegaudioparse_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegaudioparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegaudioparse_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstmpegaudioparse.h gstxingmux.h
