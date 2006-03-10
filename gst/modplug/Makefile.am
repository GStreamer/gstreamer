SUBDIRS=libmodplug .

plugin_LTLIBRARIES = libgstmodplug.la

libgstmodplug_la_SOURCES = gstmodplug.cc
libgstmodplug_la_CXXFLAGS = $(GST_CFLAGS)
libgstmodplug_la_LIBADD = $(top_builddir)/gst/modplug/libmodplug/libmodplug.la
libgstmodplug_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -lstdc++

noinst_HEADERS = gstmodplug.h
