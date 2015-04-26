plugin_LTLIBRARIES = libgstsdi.la

libgstsdi_la_SOURCES = gstsdi.c \
	gstsdidemux.c \
	gstsdimux.c

libgstsdi_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstsdi_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
		       $(GST_LIBS)
libgstsdi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsdi_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstsdidemux.h gstsdimux.h
