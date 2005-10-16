lib_LTLIBRARIES = libgstriff-@GST_MAJORMINOR@.la

libgstriff_@GST_MAJORMINOR@_la_SOURCES = \
	riff.c \
	riff-media.c \
	riff-read.c

libgstriff_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/riff
libgstriff_@GST_MAJORMINOR@include_HEADERS = \
	riff-ids.h \
	riff-media.h \
	riff-read.h

libgstriff_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS)
libgstriff_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS)
libgstriff_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LT_LDFLAGS)
