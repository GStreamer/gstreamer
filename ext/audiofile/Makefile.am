plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libafsink.la libafsrc.la

libafsink_la_SOURCES = gstafsink.c
libafsink_la_CFLAGS = $(GST_CFLAGS)
libafsink_la_LIBADD = $(GST_LIBS) $(AUDIOFILE_LIBS)

libafsrc_la_SOURCES = gstafsrc.c
libafsrc_la_CFLAGS = $(GST_CFLAGS)
libafsrc_la_LIBADD = $(GST_LIBS) $(AUDIOFILE_LIBS)

noinst_HEADERS = gstafsink.h gstafsrc.h
