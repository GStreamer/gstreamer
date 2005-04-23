plugin_LTLIBRARIES = libgstdataprotocol.la

libgstdataprotocol_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/dataprotocol
libgstdataprotocol_include_HEADERS = \
    dataprotocol.h
noinst_HEADERS = dp-private.h

libgstdataprotocol_la_SOURCES = \
    dataprotocol.c

# remove GST_ENABLE_NEW when dataprotocol has been declared API-stable
libgstdataprotocol_la_CFLAGS = $(GST_OBJ_CFLAGS) -I$(top_srcdir)/libs -DGST_ENABLE_NEW
libgstdataprotocol_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdataprotocol_la_LIBADD = $(GST_OBJ_LIBS)

