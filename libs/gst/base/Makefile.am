lib_LTLIBRARIES = libgstbase.la
AS_LIBTOOL_LIB = libgstbase

EXTRA_DIST = $(as_libtool_EXTRA_DIST)
noinst_DATA = $(as_libtool_noinst_DATA_files)

libgstbase_la_DEPENDENCIES = ../libgstreamer-@GST_MAJORMINOR@.la
libgstbase_la_SOURCES =	\
	gstbasesink.c	

libgstbase_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstbase_la_LIBADD = $(GST_OBJ_LIBS)
libgstbase_la_LDFLAGS = $(as_libtool_LDFLAGS)

noinst_HEADERS =	
	gstbasesink.h	

install-data-local: as-libtool-install-data-local

uninstall-local: as-libtool-uninstall-local

include $(top_srcdir)/common/as-libtool.mak

