plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgststereo.la

libgststereo_la_SOURCES = gststereo.c
libgststereo_la_CFLAGS = $(GST_CFLAGS)
libgststereo_la_LIBADD = $(GST_LIBS)
libgststereo_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = gststereo.h

EXTRA_DIST = README
