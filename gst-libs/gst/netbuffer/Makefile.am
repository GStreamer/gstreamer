libgstnetincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/net

libgstnetinclude_HEADERS = gstnetbuffer.h

lib_LTLIBRARIES = libgstnet-@GST_MAJORMINOR@.la

libgstnet_@GST_MAJORMINOR@_la_SOURCES = gstnetbuffer.c
# FIXME: what is that include doing here ???
libgstnet_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst/tags
libgstnet_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS)
libgstnet_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LT_LDFLAGS)
