plugin_LTLIBRARIES = libgstsdpelem.la

libgstsdpelem_la_SOURCES = gstsdpelem.c gstsdpdemux.h gstsdpdemux.c

libgstsdpelem_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstsdpelem_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
		       -lgstinterfaces-@GST_MAJORMINOR@ \
		       -lgstrtp-@GST_MAJORMINOR@ -lgstsdp-@GST_MAJORMINOR@ \
		       $(GST_LIBS) $(WIN32_LIBS)
libgstsdpelem_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsdpelem_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstsdpdemux.h
