plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstdvdec.la

libdvdec_la_SOURCES = gstdvdec.c
libdvdec_la_LIBADD = $(DV_LIBS) 
libdvdec_la_CFLAGS = $(GST_CFLAGS)

noinst_HEADERS = gstdvdec.h

EXTRA_DIST = NOTES
