SUBDIRS=libmodplug .

plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstmodplug.la

libgstmodplug_la_SOURCES = modplug_types.c gstmodplug.cc
libgstmodplug_la_CFLAGS = $(GST_CFLAGS)
libgstmodplug_la_CXXFLAGS = $(GST_CFLAGS)
libgstmodplug_la_LIBADD = $(top_builddir)/gst/modplug/libmodplug/libmodplug.la
libgstmodplug_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmodplug.h modplug_types.h

