plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstaf.la

libgstaf_la_SOURCES = gstaf.c gstafsink.c gstafsrc.c gstafparse.c
libgstaf_la_CFLAGS = $(GST_CFLAGS) $(AUDIOFILE_CFLAGS)
libgstaf_la_LIBADD = $(AUDIOFILE_LIBS)
libgstaf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstafsink.h gstafsrc.h 
