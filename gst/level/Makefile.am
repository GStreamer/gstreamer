plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstlevel.la

libgstlevel_la_SOURCES = gstlevel.c
libgstlevel_la_CFLAGS = $(GST_CFLAGS)
libgstlevel_la_LIBADD = $(GST_LIBS)
libgstlevel_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = gstlevel.h filter.func

EXTRA_DIST = README
