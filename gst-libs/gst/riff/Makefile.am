librarydir = $(prefix)/lib/gst

library_LTLIBRARIES = libgstriff.la

libgstriff_la_SOURCES = riffparse.c riffencode.c riffutil.c

libgstriffincludedir = $(includedir)/gst/riff
libgstriffinclude_HEADERS = riff.h

libgstriff_la_LIBADD = $(GST_LIBS)
libgstriff_la_CFLAGS = $(GST_CFLAGS) -funroll-all-loops -finline-functions -ffast-math
