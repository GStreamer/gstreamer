plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstaudioscale.la

libgstaudioscale_la_SOURCES = gstaudioscale.c
libgstaudioscale_la_CFLAGS = -ffast-math $(GST_CFLAGS)
libgstaudioscale_la_LIBADD = $(GST_LIBS)
libgstaudioscale_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = gstaudioscale.h
