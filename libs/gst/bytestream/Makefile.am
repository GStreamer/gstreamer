libdir = $(libdir)/gst

lib_LTLIBRARIES = libgstbytestream.la libgstbstest.la

libgstbytestream_la_SOURCES = bytestream.c 
libgstbstest_la_SOURCES = bstest.c

libgstbytestreamincludedir = $(includedir)/gst/bytestream
libgstbytestreaminclude_HEADERS = bytestream.h

libgstbytestream_la_LIBADD = $(GST_LIBS)
libgstbytestream_la_CFLAGS = $(GST_CFLAGS)

libgstbstest_la_LIBADD = $(GST_LIBS)
libgstbstest_la_CFLAGS = $(GST_CFLAGS)
