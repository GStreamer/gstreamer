plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstsid.la

libgstsid_la_SOURCES = gstsiddec.cc

noinst_HEADERS = gstsiddec.h
libgstsid_la_CXXFLAGS = $(SIDPLAY_CFLAGS) $(GST_CFLAGS)

libgstsid_la_LIBADD = $(SIDPLAY_LIBS) 
