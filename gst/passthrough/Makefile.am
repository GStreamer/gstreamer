plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstpassthrough.la

libgstpassthrough_la_SOURCES = gstpassthrough.c
libgstpassthrough_la_CFLAGS = $(GST_CFLAGS)
libgstpassthrough_la_LIBADD =
libgstpassthrough_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstpassthrough.h filter.func
