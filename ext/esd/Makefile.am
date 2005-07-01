
plugin_LTLIBRARIES = libgstesd.la

libgstesd_la_SOURCES = esdsink.c gstesd.c
#esdmon.c
libgstesd_la_CFLAGS = $(GST_CFLAGS) $(ESD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstesd_la_LIBADD = $(ESD_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_MAJORMINOR@
libgstesd_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = esdsink.h esdmon.h
EXTRA_DIST = README
