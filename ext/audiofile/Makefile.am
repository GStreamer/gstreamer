plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libafsink.la libafsrc.la

libafsink_la_SOURCES = gstafsink.c
libafsrc_la_SOURCES = gstafsrc.c
noinst_HEADERS = gstafsink.h gstafsrc.h

libafsink_la_LIBADD = $(AUDIOFILE_LIBS)
libafsrc_la_LIBADD = $(AUDIOFILE_LIBS)

libafsink_la_CFLAGS = $(GST_CFLAGS)
libafsrc_la_CFLAGS = $(GST_CFLAGS)

