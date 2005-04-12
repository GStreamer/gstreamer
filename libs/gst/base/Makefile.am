lib_LTLIBRARIES = libgstbase-@GST_MAJORMINOR@.la
AS_LIBTOOL_LIB = libgstbase-@GST_MAJORMINOR@

EXTRA_DIST = $(as_libtool_EXTRA_DIST)
noinst_DATA = $(as_libtool_noinst_DATA_files)

libgstbase_@GST_MAJORMINOR@_la_DEPENDENCIES = \
	../libgstreamer-@GST_MAJORMINOR@.la
libgstbase_@GST_MAJORMINOR@_la_SOURCES =	\
	gstbasesink.c		\
	gstbasesrc.c		\
	gstbasetransform.c	\
	gsttypefindhelper.c	

libgstbase_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstbase_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS) \
	../libgstreamer-@GST_MAJORMINOR@.la
libgstbase_@GST_MAJORMINOR@_la_LDFLAGS = $(as_libtool_LDFLAGS)

libgstbase_@GST_MAJORMINOR@includedir =		\
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/base

libgstbase_@GST_MAJORMINOR@include_HEADERS =	\
	gstbasesink.h		\
	gstbasesrc.h		\
	gstbasetransform.h	\
	gsttypefindhelper.h	

install-data-local: as-libtool-install-data-local

uninstall-local: as-libtool-uninstall-local

include $(top_srcdir)/common/as-libtool.mak

