plugin_LTLIBRARIES = libgst1394.la

libgst1394_la_SOURCES = gst1394.c gst1394probe.c gstdv1394src.c gsthdv1394src.c
libgst1394_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(DV1394_CFLAGS)
libgst1394_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstinterfaces-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(DV1394_LIBS)
libgst1394_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgst1394_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstdv1394src.h gst1394probe.h gsthdv1394src.h
