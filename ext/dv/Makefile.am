plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstdvdec.la

libgstdvdec_la_SOURCES = gstdvdec.c
libgstdvdec_la_LIBADD = $(DV_LIBS) 
libgstdvdec_la_CFLAGS = $(GST_CFLAGS)

noinst_HEADERS = gstdvdec.h

EXTRA_DIST = NOTES
