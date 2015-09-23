plugin_LTLIBRARIES = libgstflv.la

libgstflv_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstflv_la_LIBADD = -lgstpbutils-@GST_API_VERSION@ -lgstaudio-@GST_API_VERSION@ \
        -lgstvideo-@GST_API_VERSION@ -lgsttag-$(GST_API_VERSION) \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS)
libgstflv_la_LDFLAGS = ${GST_PLUGIN_LDFLAGS}
libgstflv_la_SOURCES = gstflvdemux.c gstflvmux.c
libgstflv_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstflvdemux.h gstflvmux.h amfdefs.h gstindex.h

EXTRA_DIST = gstindex.c gstmemindex.c
