
plugin_LTLIBRARIES = libgstspider.la
AS_LIBTOOL_LIB = libgstspider

EXTRA_DIST = $(as_libtool_EXTRA_DIST)
noinst_DATA = $(as_libtool_noinst_DATA_files)

libgstspider_la_SOURCES = \
	gstspider.c gstspideridentity.c \
	gstsearchfuncs.c
libgstspider_la_CFLAGS = $(GST_CFLAGS)
libgstspider_la_LIBADD =
libgstspider_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(as_libtool_LDFLAGS)

noinst_HEADERS = gstspider.h gstspideridentity.h gstsearchfuncs.h 

noinst_PROGRAMS = spidertest

spidertest_SOURCES = spidertest.c
spidertest_CFLAGS = $(GST_CFLAGS)
spidertest_LDADD = $(GST_LIBS)

install-data-local: as-libtool-install-data-local

uninstall-local: as-libtool-uninstall-local

include $(top_srcdir)/common/as-libtool.mak

