librarydir = $(libdir)/gstreamer-@GST_MAJORMINOR@

library_LTLIBRARIES = libgstaudio.la

libgstaudio_la_SOURCES = audio.c

libgstaudioincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/audio
libgstaudioinclude_HEADERS = audio.h

libgstaudio_la_LIBADD =
libgstaudio_la_CFLAGS = $(GST_CFLAGS) -finline-functions -ffast-math
libgstaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
