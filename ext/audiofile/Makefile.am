plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libafsink.la libafsrc.la

libafsink_la_SOURCES = gstafsink.c
libafsink_la_CFLAGS = $(GST_CFLAGS) $(AUDIOFILE_CFLAGS)
libafsink_la_LIBADD = $(GST_LIBS) $(AUDIOFILE_LIBS)
libafsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libafsrc_la_SOURCES = gstafsrc.c
libafsrc_la_CFLAGS = $(GST_CFLAGS) $(AUDIOFILE_CFLAGS)
libafsrc_la_LIBADD = $(GST_LIBS) $(AUDIOFILE_LIBS)
libafsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstafsink.h gstafsrc.h
