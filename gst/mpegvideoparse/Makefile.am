
plugin_LTLIBRARIES = libgstmpegvideoparse.la

libgstmpegvideoparse_la_SOURCES = mpegvideoparse.c mpegpacketiser.c
libgstmpegvideoparse_la_CFLAGS = $(GST_CFLAGS)
libgstmpegvideoparse_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegvideoparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpegvideoparse_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = mpegvideoparse.h mpegpacketiser.h
