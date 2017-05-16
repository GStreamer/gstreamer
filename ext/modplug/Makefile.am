plugin_LTLIBRARIES = libgstmodplug.la

libgstmodplug_la_SOURCES = gstmodplug.cc
libgstmodplug_la_CXXFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) $(MODPLUG_CFLAGS)
libgstmodplug_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(LIBM) $(MODPLUG_LIBS)
libgstmodplug_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmodplug.h
