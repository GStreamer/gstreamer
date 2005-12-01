# FIXME:
# need to get gstbufferstore.[ch] into its own lib, preferably 
# libs/gst/bufferstore
# This requires building libs/gst before this dir, which we currently don't
# do.

plugin_LTLIBRARIES = libgstcoreelements.la

if HAVE_SYS_SOCKET_H
GSTFDSRC = gstfdsrc.c
else
GSTFDSRC =
endif

libgstcoreelements_la_DEPENDENCIES = $(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
libgstcoreelements_la_SOURCES =	\
	gstbufferstore.c	\
	gstcapsfilter.c		\
	gstelements.c		\
	gstfakesrc.c		\
	gstfakesink.c		\
	$(GSTFDSRC)		\
	gstfilesink.c		\
	gstfilesrc.c		\
	gstidentity.c		\
	gstqueue.c		\
	gsttee.c		\
	gsttypefindelement.c

libgstcoreelements_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstcoreelements_la_LIBADD = \
	$(top_builddir)/libs/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(GST_OBJ_LIBS)
libgstcoreelements_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS =		\
	gstbufferstore.h	\
	gstfakesink.h		\
	gstfakesrc.h		\
	gstfdsrc.h		\
	gstfilesink.h		\
	gstfilesrc.h		\
	gstidentity.h		\
	gstqueue.h		\
	gsttee.h		\
	gsttypefindelement.h

EXTRA_DIST = gstfdsrc.c
