librarydir = $(libdir)/gstreamer-@GST_MAJORMINOR@

library_LTLIBRARIES = libgstbytestream.la

libgstbytestreamincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst
libgstbytestreaminclude_HEADERS = bytestream.h

libgstbytestream_la_SOURCES = bytestream.c 
libgstbytestream_la_CFLAGS = $(GST_CFLAGS)
libgstbytestream_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
