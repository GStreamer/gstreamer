plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstdivx.la

libgstdivx_la_SOURCES = gstdivxenc.c
libgstdivx_la_CFLAGS = $(GST_CFLAGS) $(DIVX_CFLAGS)
libgstdivx_la_LIBADD = $(DIVX_LIBS)
libgstdivx_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstdivxenc.h
