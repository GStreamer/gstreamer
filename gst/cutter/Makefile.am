plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstcutter.la

libgstcutter_la_SOURCES = gstcutter.c
libgstcutter_la_CFLAGS = $(GST_CFLAGS)
libgstcutter_la_LIBADD = $(GST_LIBS)

noinst_HEADERS = gstcutter.h filter.func

EXTRA_DIST = README
