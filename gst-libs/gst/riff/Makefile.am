librarydir = $(prefix)/lib/gstreamer-@GST_MAJORMINOR@

library_LTLIBRARIES = libgstriff.la

libgstriff_la_SOURCES = riffparse.c riffencode.c riffutil.c riff.c

## libgstriffincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/riff
## libgstriffinclude_HEADERS = riff.h
noinst_HEADERS = riff.h

libgstriff_la_LIBADD =
libgstriff_la_CFLAGS = $(GST_CFLAGS) -funroll-all-loops -finline-functions -ffast-math
libgstriff_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
