plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstdivxenc.la libgstdivxdec.la

libgstdivxenc_la_SOURCES = gstdivxenc.c
libgstdivxenc_la_CFLAGS = $(GST_CFLAGS) $(DIVX_CFLAGS)
libgstdivxenc_la_LIBADD = $(DIVXENC_LIBS)
libgstdivxenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstdivxdec_la_SOURCES = gstdivxdec.c
libgstdivxdec_la_CFLAGS = $(GST_CFLAGS) $(DIVX_CFLAGS)
libgstdivxdec_la_LIBADD = $(DIVXDEC_LIBS)
libgstdivxdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstdivxenc.h gstdivxdec.h
