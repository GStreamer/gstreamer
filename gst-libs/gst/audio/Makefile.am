librarydir = $(libdir)/gstreamer-@GST_MAJORMINOR@

library_LTLIBRARIES = libgstaudio.la libgstaudiofilter.la libgstaudiofilterexample.la

libgstaudio_la_SOURCES = audio.c audioclock.c

libgstaudioincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/audio
libgstaudioinclude_HEADERS = audio.h audioclock.h gstaudiofilter.h

libgstaudio_la_LIBADD =
libgstaudio_la_CFLAGS = $(GST_CFLAGS)
libgstaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstaudiofilter_la_SOURCES = gstaudiofilter.c gstaudiofilter.h
libgstaudiofilter_la_CFLAGS = $(GST_CFLAGS)
libgstaudiofilter_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstaudiofilterexample_la_SOURCES = gstaudiofilterexample.c
libgstaudiofilterexample_la_CFLAGS = $(GST_CFLAGS)
libgstaudiofilterexample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

