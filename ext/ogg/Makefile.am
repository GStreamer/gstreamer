plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstogg.la

libgstogg_la_SOURCES = \
	gstogg.c \
	gstoggdemux.c \
	gstoggmux.c \
	gstogmparse.c

libgstogg_la_CFLAGS = $(GST_CFLAGS) $(OGG_CFLAGS)
libgstogg_la_LIBADD = $(OGG_LIBS) $(top_builddir)/gst-libs/gst/riff/libgstriff.la
libgstogg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
