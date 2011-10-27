plugin_LTLIBRARIES = libgstpng.la

libgstpng_la_SOURCES = gstpng.c gstpngenc.c gstpngdec.c
libgstpng_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(LIBPNG_CFLAGS)
libgstpng_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@ \
	$(GST_LIBS) $(LIBPNG_LIBS)
libgstpng_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpng_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstpngdec.h gstpngenc.h
