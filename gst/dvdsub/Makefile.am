plugin_LTLIBRARIES = libgstdvdsub.la

libgstdvdsub_la_SOURCES = gstdvdsubdec.c gstdvdsubparse.c
libgstdvdsub_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstdvdsub_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstdvdsub_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdvdsub_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstdvdsubdec.h gstdvdsubparse.h
