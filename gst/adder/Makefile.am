plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstadder.la

libgstadder_la_SOURCES = gstadder.c
libgstadder_la_CFLAGS = $(GST_CFLAGS)
libgstadder_la_LIBADD = $(GST_LIBS)
libgstadder_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = gstadder.h
