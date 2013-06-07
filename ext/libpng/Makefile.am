plugin_LTLIBRARIES = libgstpng.la

libgstpng_la_SOURCES = gstpng.c gstpngenc.c gstpngdec.c
libgstpng_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(LIBPNG_CFLAGS)
libgstpng_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBPNG_LIBS)
libgstpng_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpng_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstpngdec.h gstpngenc.h
