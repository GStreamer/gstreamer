plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstafsink.la libgstafsrc.la

libgstafsink_la_SOURCES = gstafsink.c
libgstafsink_la_CFLAGS = $(GST_CFLAGS) $(AUDIOFILE_CFLAGS)
libgstafsink_la_LIBADD = $(AUDIOFILE_LIBS)
libgstafsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstafsrc_la_SOURCES = gstafsrc.c
libgstafsrc_la_CFLAGS = $(GST_CFLAGS) $(AUDIOFILE_CFLAGS)
libgstafsrc_la_LIBADD = $(AUDIOFILE_LIBS)
libgstafsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstafsink.h gstafsrc.h
