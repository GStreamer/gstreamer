plugin_LTLIBRARIES = libgstaccurip.la

libgstaccurip_la_SOURCES = gstaccurip.c gstaccurip.h

libgstaccurip_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	$(ACCURIP_CFLAGS)
libgstaccurip_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) \
	$(ACCURIP_LIBS)
libgstaccurip_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaccurip_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstaccurip.h
