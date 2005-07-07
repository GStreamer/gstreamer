
plugin_LTLIBRARIES = libgst1394.la

libgst1394_la_SOURCES = gst1394.c gstdv1394src.c
libgst1394_la_CFLAGS = $(GST_CFLAGS)
libgst1394_la_LIBADD = $(DV1394_LIBS) 
libgst1394_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_BASE_LIBS)

noinst_HEADERS = gstdv1394src.h
