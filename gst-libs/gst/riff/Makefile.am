librarydir = $(prefix)/lib/gst

library_LTLIBRARIES = libgstriff.la

libgstriff_la_SOURCES = riffparse.c riffencode.c riffutil.c riff.c

libgstriffincludedir = $(includedir)/@PACKAGE@-@VERSION@/gst/riff
libgstriffinclude_HEADERS = riff.h

libgstriff_la_LIBADD =
libgstriff_la_CFLAGS = $(GST_CFLAGS) -funroll-all-loops -finline-functions -ffast-math
libgstriff_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
