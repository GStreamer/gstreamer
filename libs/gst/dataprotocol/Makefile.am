lib_LTLIBRARIES = libgstdataprotocol-@GST_MAJORMINOR@.la

libgstdataprotocol_@GST_MAJORMINOR@_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/dataprotocol
libgstdataprotocol_@GST_MAJORMINOR@_include_HEADERS = \
    dataprotocol.h
noinst_HEADERS = dp-private.h

libgstdataprotocol_@GST_MAJORMINOR@_la_SOURCES = \
    dataprotocol.c

libgstdataprotocol_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstdataprotocol_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS)
libgstdataprotocol_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

