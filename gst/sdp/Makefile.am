plugin_LTLIBRARIES = libgstsdpelem.la

libgstsdpelem_la_SOURCES = gstsdpelem.c gstsdpsrc.c gstsdpdemux.c

libgstsdpelem_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)
libgstsdpelem_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GIO_LIBS) \
		       -lgstrtp-@GST_API_VERSION@ -lgstsdp-@GST_API_VERSION@ \
		       -lgstapp-@GST_API_VERSION@ \
		       $(GST_LIBS) $(WINSOCK2_LIBS)
libgstsdpelem_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsdpelem_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstsdpdemux.h gstsdpsrc.h
