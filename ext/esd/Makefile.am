plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libesdsink.la libesdmon.la

libesdsink_la_SOURCES = esdsink.c
libesdsink_la_CFLAGS = $(GST_CFLAGS) $(ESD_CFLAGS)
libesdsink_la_LIBADD = $(GST_LIBS) $(ESD_LIBS)
libesdsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libesdmon_la_SOURCES = esdmon.c
libesdmon_la_CFLAGS = $(GST_CFLAGS) $(ESD_CFLAGS)
libesdmon_la_LIBADD = $(GST_LIBS) $(ESD_LIBS)
libesdmon_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = esdsink.h
EXTRA_DIST = README
