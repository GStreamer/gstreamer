lib_LTLIBRARIES = libgstbytestream-@GST_MAJORMINOR@.la
AS_LIBTOOL_LIB = libgstbytestream-@GST_MAJORMINOR@

EXTRA_DIST = $(as_libtool_EXTRA_DIST)
noinst_DATA = $(as_libtool_noinst_DATA_files)

libgstbytestreamincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/bytestream
libgstbytestreaminclude_HEADERS = adapter.h filepad.h

libgstbytestream_@GST_MAJORMINOR@_la_SOURCES = adapter.c filepad.c
libgstbytestream_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstbytestream_@GST_MAJORMINOR@_la_LIBS = $(GST_OBJ_LIBS)
libgstbytestream_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_OBJ_LDFLAGS) $(as_libtool_LDFLAGS)

install-data-local: as-libtool-install-data-local

uninstall-local: as-libtool-uninstall-local

include $(top_srcdir)/common/as-libtool.mak
