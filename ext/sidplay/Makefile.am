plugin_LTLIBRARIES = libgstsid.la

libgstsid_la_SOURCES = gstsiddec.cc
libgstsid_la_CXXFLAGS = $(GST_CFLAGS) $(SIDPLAY_CFLAGS) $(GST_PLUGINS_CFLAGS)
libgstsid_la_LIBADD = $(SIDPLAY_LIBS) $(GST_PLUGINS_LIBS)
libgstsid_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstsiddec.h
