librarydir = $(libdir)/gst

library_LTLIBRARIES = libgstaudio.la

libgstaudio_la_SOURCES = audio.c

libgstaudioincludedir = $(includedir)/gst/audio
libgstaudioinclude_HEADERS = audio.h

libgstaudio_la_LIBADD = $(GST_LIBS)
libgstaudio_la_CFLAGS = $(GST_CFLAGS) -finline-functions -ffast-math
