plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstmodplug.la

libgstmodplug_la_SOURCES = gstmodplug.cc
libgstmodplug_la_CXXFLAGS = $(GST_CFLAGS)
libgstmodplug_la_LIBADD = $(GST_LIBS) libmodplug/libmodplug.la
libgstmodplug_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

SUBDIRS=libmodplug

noinst_HEADERS = gstmodplug.h

