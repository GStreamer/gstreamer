plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstpng.la

libgstpng_la_SOURCES = gstpng.c gstpngenc.c
libgstpng_la_CFLAGS = $(GST_CFLAGS)
libgstpng_la_LIBADD = $(GST_LIBS) $(LIBPNG_LIBS)
libgstpng_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstpngenc.h

