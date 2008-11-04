SUBDIRS=libmodplug .

plugin_LTLIBRARIES = libgstmodplug.la

libgstmodplug_la_SOURCES = gstmodplug.cc
libgstmodplug_la_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS)
libgstmodplug_la_LIBADD = $(top_builddir)/gst/modplug/libmodplug/libmodplug.la $(GST_PLUGINS_BASE_LIBS) -lstdc++
libgstmodplug_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmodplug_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstmodplug.h
