filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstaudio.la

libgstaudio_la_SOURCES = gstaudio.c

libgstaudioincludedir = $(includedir)/gst/libs/gstaudio
libgstaudioinclude_HEADERS = gstaudio.h

# FIXME is this needed?
libgstaudio_la_CFLAGS = -O2 $(FOMIT_FRAME_POINTER) -finline-functions -ffast-math $(GLIB_CFLAGS) $(XML_CFLAGS)  $(GST_CFLAGS)
