SUBDIRS=libmodplug .

plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstmodplug.la

libgstmodplug_la_SOURCES = gstmodplug.cc modplug_types.cc
libgstmodplug_la_CFLAGS = $(GST_CFLAGS)
libgstmodplug_la_CXXFLAGS = $(GST_CFLAGS)
libgstmodplug_la_LIBADD = libmodplug/libmodplug.la -lstdc++
libgstmodplug_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmodplug.h modplug_types.h

