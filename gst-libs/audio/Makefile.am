libdir = $(libdir)/gst

lib_LTLIBRARIES = libgstaudio.la

libgstaudio_la_SOURCES = gstaudio.c

libgstaudioincludedir = $(includedir)/gst/libs/gstaudio
libgstaudioinclude_HEADERS = gstaudio.h

libgstaudio_la_LIBADD = $(GST_LIBS)
libgstaudio_la_CFLAGS = $(GST_CFLAGS) -finline-functions -ffast-math
# FIXME is this needed?
## from merge, kept for reference 
## libgstaudio_la_CFLAGS = -O2 $(FOMIT_FRAME_POINTER) -finline-functions -ffast-math $(GLIB_CFLAGS) $(XML_CFLAGS)  $(GST_CFLAGS)
