plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstaudioscale.la

libgstaudioscale_la_SOURCES = gstaudioscale.c
libgstaudioscale_la_CFLAGS = -ffast-math $(GST_CFLAGS)
libgstaudioscale_la_LIBADD = $(GST_LIBS)

noinst_HEADERS = gstaudioscale.h
