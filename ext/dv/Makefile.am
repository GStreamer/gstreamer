plugin_LTLIBRARIES = libgstdv.la

libgstdv_la_SOURCES = gstdv.c gstdvdec.c gstdvdemux.c
libgstdv_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(LIBDV_CFLAGS)
libgstdv_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) \
	 $(GST_BASE_LIBS) $(LIBDV_LIBS)
libgstdv_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdv_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstdvdemux.h gstdvdec.h

EXTRA_DIST = NOTES
