plugin_LTLIBRARIES = libgstbytestream.la
AS_LIBTOOL_LIB = libgstbytestream

EXTRA_DIST = $(as_libtool_EXTRA_DIST)
noinst_DATA = $(as_libtool_noinst_DATA_files)

libgstbytestreamincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/bytestream
libgstbytestreaminclude_HEADERS = bytestream.h adapter.h filepad.h

libgstbytestream_la_SOURCES = bytestream.c adapter.c filepad.c
libgstbytestream_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstbytestream_la_LIBS = $(GST_OBJ_LIBS)
libgstbytestream_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(as_libtool_LDFLAGS)

install-data-local: as-libtool-install-data-local

uninstall-local: as-libtool-uninstall-local

include $(top_srcdir)/common/as-libtool.mak
