plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstparseau.la

libgstparseau_la_SOURCES = gstparseau.c
libgstparseau_la_CFLAGS = $(GST_CFLAGS)
libgstparseau_la_LIBADD = $(GST_LIBS)
libgstparseau_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = gstparseau.h
