plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstrtjpeg.la

libgstrtjpeg_la_SOURCES = gstrtjpeg.c gstrtjpegenc.c gstrtjpegdec.c RTjpeg.c
libgstrtjpeg_la_CFLAGS = $(GST_CFLAGS)
libgstrtjpeg_la_LIBADD = $(GST_LIBS)
libgstrtjpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstrtjpegenc.h gstrtjpegdec.h RTjpeg.h
