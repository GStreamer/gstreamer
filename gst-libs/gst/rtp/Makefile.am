libgstrtpincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/rtp

libgstrtpinclude_HEADERS = gstrtpbuffer.h gstbasertpdepayload.h

lib_LTLIBRARIES = libgstrtp-@GST_MAJORMINOR@.la

libgstrtp_@GST_MAJORMINOR@_la_SOURCES = gstrtpbuffer.c gstbasertpdepayload.c
libgstrtp_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) -I$(top_srcdir)/gst/tags
libgstrtp_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS)

