
plugin_LTLIBRARIES = libgstindexers.la
AS_LIBTOOL_LIB = libgstindexers

EXTRA_DIST = $(as_libtool_EXTRA_DIST)
noinst_DATA = $(as_libtool_noinst_DATA_files)

# file index uses xml
if GST_DISABLE_LOADSAVE
GST_LOADSAVE_SRC =
else
GST_LOADSAVE_SRC = gstfileindex.c
endif

libgstindexers_la_SOURCES = gstindexers.c gstmemindex.c $(GST_LOADSAVE_SRC)
libgstindexers_la_CFLAGS = $(GST_CFLAGS)
libgstindexers_la_LIBADD =
libgstindexers_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

install-data-local: as-libtool-install-data-local

uninstall-local: as-libtool-uninstall-local

include $(top_srcdir)/common/as-libtool.mak

