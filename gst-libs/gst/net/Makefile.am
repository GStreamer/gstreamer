libgstnetincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/net

libgstnetinclude_HEADERS = gstnetbuffer.h

lib_LTLIBRARIES = libgstnet-@GST_MAJORMINOR@.la

libgstnet_@GST_MAJORMINOR@_la_SOURCES = gstnetbuffer.c
libgstnet_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS)
libgstnet_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS)
libgstnet_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)
