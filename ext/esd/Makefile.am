plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libesdsink.la

libesdsink_la_SOURCES = esdsink.c
libesdsink_la_CFLAGS = $(GST_CFLAGS) $(ESD_CFLAGS)
libesdsink_la_LIBADD = $(GST_LIBS) $(ESD_LIBS)

noinst_HEADERS = esdsink.h
EXTRA_DIST = README
