lib_LTLIBRARIES = libgstbase-@GST_MAJORMINOR@.la

libgstbase_@GST_MAJORMINOR@_la_DEPENDENCIES = \
	$(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
libgstbase_@GST_MAJORMINOR@_la_SOURCES = \
	gstadapter.c		\
	gstbasesink.c		\
	gstbasesrc.c		\
	gstbasetransform.c	\
	gstbitreader.c		\
	gstbytereader.c		\
	gstcollectpads.c	\
	gstpushsrc.c		\
	gsttypefindhelper.c	\
	gstdataqueue.c

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
	gstbitreader.h		\
	gstbytereader.h		\
	gstcollectpads.h	\
	gstpushsrc.h		\
	gsttypefindhelper.h	\
	gstdataqueue.h

CLEANFILES = *.gcno *.gcda *.gcov

%.c.gcov: .libs/libgstbase_@GST_MAJORMINOR@_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstbase_@GST_MAJORMINOR@_la_SOURCES:=.gcov)
