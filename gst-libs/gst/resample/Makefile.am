librarydir = $(libdir)/gstreamer-@GST_MAJORMINOR@

library_LTLIBRARIES = libgstresample.la

libgstresample_la_SOURCES = dtos.c dtof.c functable.c resample.c resample.h
libgstresample_la_LIBADD =
libgstresample_la_CFLAGS = $(GST_CFLAGS)
libgstresample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstresampleincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/resample
libgstresampleinclude_HEADERS = resample.h

noinst_HEADERS = private.h

