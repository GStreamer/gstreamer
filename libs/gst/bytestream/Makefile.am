librarydir = $(libdir)/gst

library_LTLIBRARIES = libgstbytestream.la
noinst_LTLIBRARIES = libgstbstest.la

libgstbytestreamincludedir = $(includedir)/gst/bytestream
libgstbytestreaminclude_HEADERS = bytestream.h

libgstbytestream_la_SOURCES = bytestream.c 
libgstbytestream_la_CFLAGS = $(GST_CFLAGS)
libgstbytestream_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstbstest_la_SOURCES = bstest.c
libgstbstest_la_CFLAGS = $(GST_CFLAGS)
