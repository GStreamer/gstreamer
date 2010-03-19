plugin_LTLIBRARIES = libgstsid.la

libgstsid_la_SOURCES = gstsiddec.cc
libgstsid_la_CXXFLAGS = $(GST_BASE_CFLAGS) $(GST_CXXFLAGS) $(SIDPLAY_CFLAGS)
libgstsid_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(SIDPLAY_LIBS)
libgstsid_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsid_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstsiddec.h
