plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstshout.la

libgstshout_la_SOURCES = gstshout.c 
libgstshout_la_CFLAGS = $(GST_CFLAGS)
libgstshout_la_LIBADD = $(GST_LIBS) $(SHOUT_LIBS)
libgstshout_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstshout.h
