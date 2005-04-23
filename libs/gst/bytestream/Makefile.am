plugin_LTLIBRARIES = libgstbytestream.la

libgstbytestreamincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/bytestream
libgstbytestreaminclude_HEADERS = bytestream.h adapter.h filepad.h

libgstbytestream_la_SOURCES = bytestream.c adapter.c filepad.c
libgstbytestream_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstbytestream_la_LIBS = $(GST_OBJ_LIBS)
libgstbytestream_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

