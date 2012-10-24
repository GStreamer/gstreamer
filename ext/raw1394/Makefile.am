plugin_LTLIBRARIES = libgst1394.la

if USE_LIBIEC61883
hdvsource = gsthdv1394src.c
hdvheaders = gsthdv1394src.h
else
hdvsource = 
hdvheaders = 
endif

libgst1394_la_SOURCES = \
	gst1394.c gst1394probe.c gstdv1394src.c $(hdvsource) \
	gst1394clock.c
libgst1394_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(DV1394_CFLAGS)
libgst1394_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(DV1394_LIBS)
libgst1394_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgst1394_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstdv1394src.h gst1394probe.h $(hdvheaders) \
	gst1394clock.h

