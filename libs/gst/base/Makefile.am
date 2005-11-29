lib_LTLIBRARIES = libgstbase-@GST_MAJORMINOR@.la

libgstbase_@GST_MAJORMINOR@_la_DEPENDENCIES = \
	$(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
libgstbase_@GST_MAJORMINOR@_la_SOURCES =	\
	gstadapter.c		\
	gstbasesink.c		\
	gstbasesrc.c		\
	gstbasetransform.c	\
	gstcollectpads.c	\
	gstpushsrc.c		\
	gsttypefindhelper.c	

libgstbase_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstbase_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS)
libgstbase_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

libgstbase_@GST_MAJORMINOR@includedir =		\
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/base

libgstbase_@GST_MAJORMINOR@include_HEADERS =	\
	gstadapter.h		\
	gstbasesink.h		\
	gstbasesrc.h		\
	gstbasetransform.h	\
	gstcollectpads.h	\
	gstpushsrc.h		\
	gsttypefindhelper.h	

