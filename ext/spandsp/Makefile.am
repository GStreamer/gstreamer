plugin_LTLIBRARIES = libgstspandsp.la

libgstspandsp_la_SOURCES = gstspandsp.c gstspanplc.c gstdtmfdetect.c
libgstspandsp_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(SPANDSP_CFLAGS)
libgstspandsp_la_LIBADD = $(SPANDSP_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	 $(GST_BASE_LIBS) $(GST_LIBS)
libgstspandsp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstspandsp_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstspanplc.h gstdtmfdetect.h
