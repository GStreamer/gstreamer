plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstvolume.la

libgstvolume_la_SOURCES = gstvolume.c
libgstvolume_la_CFLAGS = $(GST_CFLAGS)
libgstvolume_la_LIBADD = $(GST_LIBS)
libgstvolume_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = gstvolume.h filter.func
