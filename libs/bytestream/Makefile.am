filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstbytestream.la libgstbstest.la

libgstbytestream_la_SOURCES = gstbytestream.c 
libgstbytestream_la_CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)
libgstbstest_la_SOURCES = gstbstest.c
libgstbstest_la_CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)

libgstbytestreamincludedir = $(includedir)/gst/libs/bytestream
libgstbytestreaminclude_HEADERS = gstbytestream.h

libgstbytestream_la_LIBADD = $(GST_LIBS)
libgstbytestream_la_CFLAGS = $(GST_CFLAGS)

libgstbstest_la_LIBADD = $(GST_LIBS)
libgstbstest_la_CFLAGS = $(GST_CFLAGS)
