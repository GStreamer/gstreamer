plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgsttheora.la

libgsttheora_la_SOURCES = theoradec.c
libgsttheora_la_CFLAGS = $(GST_CFLAGS) $(THEORA_CFLAGS)
libgsttheora_la_LIBADD = $(THEORA_LIBS)
libgsttheora_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

