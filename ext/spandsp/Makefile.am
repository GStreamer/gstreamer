plugin_LTLIBRARIES = libgstspandsp.la

libgstspandsp_la_SOURCES = gstspandsp.c gstspanplc.c
libgstspandsp_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(SPANDSP_CFLAGS)
libgstspandsp_la_LIBADD = $(SPANDSP_LIBS) $(GST_PLUGINS_BASE_LIBS) $(GST_LIBS)
libgstspandsp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstspandsp_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstspanplc.h
