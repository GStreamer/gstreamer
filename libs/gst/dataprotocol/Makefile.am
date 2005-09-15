lib_LTLIBRARIES = libgstdataprotocol-@GST_MAJORMINOR@.la

libgstdataprotocol_@GST_MAJORMINOR@_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/dataprotocol
libgstdataprotocol_@GST_MAJORMINOR@_include_HEADERS = \
    dataprotocol.h
noinst_HEADERS = dp-private.h

libgstdataprotocol_@GST_MAJORMINOR@_la_SOURCES = \
    dataprotocol.c

# remove GST_ENABLE_NEW when dataprotocol has been declared API-stable
libgstdataprotocol_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS) -I$(top_srcdir)/libs -DGST_ENABLE_NEW
libgstdataprotocol_@GST_MAJORMINOR@_la_LDFLAGS =  @GST_LIB_LDFLAGS@ \
	-no-undefined
libgstdataprotocol_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS)

