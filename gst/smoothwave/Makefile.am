filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstsmoothwave.la

libgstsmoothwave_la_SOURCES = gstsmoothwave.c
libgstsmoothwave_la_CFLAGS = $(GST_CFLAGS) $(GTK_CFLAGS)
libgstsmoothwave_la_LIBADD = $(GTK_LIBS)

noinst_HEADERS = gstsmoothwave.h

EXTRA_DIST = README
