plugin_LTLIBRARIES = libgstriff.la

libgstriff_la_SOURCES = \
	riff.c \
	riff-media.c \
	riff-read.c

libgstriffincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/riff
libgstriffinclude_HEADERS = \
	riff-ids.h \
	riff-media.h \
	riff-read.h

libgstriff_la_LIBADD =
libgstriff_la_CFLAGS = $(GST_CFLAGS)
libgstriff_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
