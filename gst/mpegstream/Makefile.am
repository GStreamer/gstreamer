
plugin_LTLIBRARIES = libgstmpegstream.la

libgstmpegstream_la_SOURCES = gstmpegstream.c gstmpegparse.c gstmpegdemux.c gstmpegpacketize.c gstrfc2250enc.c gstmpegclock.c
libgstmpegstream_la_CFLAGS = $(GST_CFLAGS)
libgstmpegstream_la_LIBADD =
libgstmpegstream_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmpegparse.h gstmpegdemux.h gstmpegpacketize.h gstrfc2250enc.h gstmpegclock.h

EXTRA_DIST = README notes
