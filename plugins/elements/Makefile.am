
plugin_LTLIBRARIES = libgstcoreelements.la

libgstcoreelements_la_DEPENDENCIES = $(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
libgstcoreelements_la_SOURCES =	\
	gstcapsfilter.c		\
	gstelements.c		\
	gstfakesrc.c		\
	gstfakesink.c		\
	gstfdsrc.c		\
	gstfdsink.c		\
	gstfilesink.c		\
	gstfilesrc.c		\
	gstfunnel.c		\
	gstidentity.c		\
	gstinputselector.c	\
	gstoutputselector.c	\
	gstmultiqueue.c		\
	gstqueue.c		\
	gstqueue2.c		\
	gsttee.c		\
	gsttypefindelement.c	\
	gstvalve.c

libgstcoreelements_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstcoreelements_la_LIBADD = \
	$(top_builddir)/libs/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(GST_OBJ_LIBS)
libgstcoreelements_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstcoreelements_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS =		\
	gstcapsfilter.h		\
	gstfakesink.h		\
	gstfakesrc.h		\
	gstfdsrc.h		\
	gstfdsink.h		\
	gstfilesink.h		\
	gstfilesrc.h		\
	gstfunnel.h		\
	gstidentity.h		\
	gstinputselector.h	\
	gstoutputselector.h	\
	gstmultiqueue.h		\
	gstqueue.h		\
	gstqueue2.h		\
	gsttee.h		\
	gsttypefindelement.h	\
	gstvalve.h

EXTRA_DIST = gstfdsrc.c \
	     gstfdsink.c


CLEANFILES = *.gcno *.gcda *.gcov *.gcov.out

%.c.gcov: .libs/libgstcoreelements_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstcoreelements_la_SOURCES:=.gcov)
