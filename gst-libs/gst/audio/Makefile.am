librarydir = $(libdir)/gstreamer-@GST_MAJORMINOR@

library_LTLIBRARIES = libgstaudio.la

libgstaudio_la_SOURCES = audio.c audioclock.c

libgstaudioincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/audio
libgstaudioinclude_HEADERS = audio.h audioclock.h

libgstaudio_la_LIBADD =
libgstaudio_la_CFLAGS = $(GST_CFLAGS) $(GST_OPT_CFLAGS)
libgstaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
