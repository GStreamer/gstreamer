plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstesdsink.la libgstesdmon.la

libgstesdsink_la_SOURCES = esdsink.c
libgstesdsink_la_CFLAGS = $(GST_CFLAGS) $(ESD_CFLAGS)
libgstesdsink_la_LIBADD = $(ESD_LIBS)
libgstesdsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstesdmon_la_SOURCES = esdmon.c
libgstesdmon_la_CFLAGS = $(GST_CFLAGS) $(ESD_CFLAGS)
libgstesdmon_la_LIBADD = $(ESD_LIBS)
libgstesdmon_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = esdsink.h
EXTRA_DIST = README
