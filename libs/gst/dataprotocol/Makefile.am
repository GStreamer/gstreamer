
lib_LTLIBRARIES = libgstdataprotocol.la
AS_LIBTOOL_LIB = libgstdataprotocol

EXTRA_DIST = $(as_libtool_EXTRA_DIST)
noinst_DATA = $(as_libtool_noinst_DATA_files)

libgstdataprotocol_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/dataprotocol
libgstdataprotocol_include_HEADERS = \
    dataprotocol.h
noinst_HEADERS = dp-private.h

libgstdataprotocol_la_SOURCES = \
    dataprotocol.c

libgstdataprotocol_la_CFLAGS = $(GST_OBJ_CFLAGS) -I$(top_srcdir)/libs
libgstdataprotocol_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(as_libtool_LDFLAGS)
libgstdataprotocol_la_LIBADD = $(GST_OBJ_LIBS)

install-data-local: as-libtool-install-data-local

uninstall-local: as-libtool-uninstall-local

include $(top_srcdir)/common/as-libtool.mak

# regression test
check_PROGRAMS = dataprotocol-test
dataprotocol_test_SOURCES = dataprotocol-test.c dataprotocol.c
dataprotocol_test_CFLAGS = $(GST_LIB_CFLAGS) -I$(top_srcdir)/libs
dataprotocol_test_LDADD = $(GST_OBJ_LIBS)

