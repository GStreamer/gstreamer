filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstaudio.la

libgstaudio_la_SOURCES = gstaudio.c

libgstaudioincludedir = $(includedir)/gst/libs/gstaudio
libgstaudioinclude_HEADERS = gstaudio.h

libgstaudio_la_LIBADD = $(GST_LIBS)
libgstaudio_la_CFLAGS = $(GST_CFLAGS) -finline-functions -ffast-math
