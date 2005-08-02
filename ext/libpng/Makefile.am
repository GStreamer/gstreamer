
plugin_LTLIBRARIES = libgstpng.la

libgstpng_la_SOURCES = gstpng.c gstpngenc.c # gstpngdec.c
libgstpng_la_CFLAGS = $(GST_CFLAGS)
libgstpng_la_LIBADD = $(GST_LIBS) $(LIBPNG_LIBS)
libgstpng_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstpngdec.h gstpngenc.h
