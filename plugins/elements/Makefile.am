
plugin_LTLIBRARIES = libgstcoreelements.la

if HAVE_SYS_SOCKET_H
GSTFDSRC = gstfdsrc.c
GSTFDSINK = gstfdsink.c
else
GSTFDSRC =
GSTFDSINK =
endif

libgstcoreelements_la_DEPENDENCIES = $(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
libgstcoreelements_la_SOURCES =	\
	gstcapsfilter.c		\
	gstelements.c		\
	gstfakesrc.c		\
	gstfakesink.c		\
	$(GSTFDSRC)		\
	$(GSTFDSINK)		\
	gstfilesink.c		\
	gstfilesrc.c		\
	gstidentity.c		\
	gstqueue.c		\
	gstqueue2.c		\
	gsttee.c		\
	gsttypefindelement.c	\
	gstmultiqueue.c

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
	gstidentity.h		\
	gstqueue.h		\
	gstqueue2.h		\
	gsttee.h		\
	gsttypefindelement.h	\
	gstmultiqueue.h

EXTRA_DIST = gstfdsrc.c \
	     gstfdsink.c


CLEANFILES = *.gcno *.gcda *.gcov *.gcov.out

%.c.gcov: .libs/libgstcoreelements_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstcoreelements_la_SOURCES:=.gcov)
