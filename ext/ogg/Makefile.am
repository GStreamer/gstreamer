plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstogg.la

libgstogg_la_SOURCES = gstoggdemux.c gstoggmux.c gstogg.c
libgstogg_la_CFLAGS = $(GST_CFLAGS) $(OGG_CFLAGS)
libgstogg_la_LIBADD = $(OGG_LIBS)
libgstogg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

