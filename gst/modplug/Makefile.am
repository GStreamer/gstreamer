SUBDIRS=libmodplug .

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstmodplug.la

libgstmodplug_la_SOURCES = modplug_types.cc gstmodplug.cc
libgstmodplug_la_CPPFLAGS = $(GST_CFLAGS)
libgstmodplug_la_CXXFLAGS = $(libgstmodplug_la_CPPFLAGS)
libgstmodplug_la_LIBADD = $(top_builddir)/gst/modplug/libmodplug/libmodplug.la
libgstmodplug_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmodplug.h modplug_types.h
