# FIXME:
# need to get gstbufferstore.[ch] into its own lib, preferrably 
# libs/gst/buifferstore
# This requires building libs/gst before this dir, which we currently don't
# do.

plugin_LTLIBRARIES = libgstelements.la

if HAVE_SYS_SOCKET_H
GSTFDSRC = gstfdsrc.c
else
GSTFDSRC =
endif

libgstelements_la_DEPENDENCIES = ../libgstreamer-@GST_MAJORMINOR@.la
libgstelements_la_SOURCES =	\
	gstbufferstore.c	\
	gstcapsfilter.c		\
	gstfakesrc.c		\
	gstfakesink.c		\
	$(GSTFDSRC)		\
	gstfilesink.c		\
	gstfilesrc.c		\
	gstidentity.c		\
	gstelements.c		\
	gsttee.c		\
	gsttypefindelement.c

libgstelements_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstelements_la_LIBADD = \
	$(top_builddir)/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(GST_OBJ_LIBS)
libgstelements_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS =		\
	gstbufferstore.h	\
	gstfakesink.h		\
	gstfakesrc.h		\
	gstfdsrc.h		\
	gstfilesink.h		\
	gstfilesrc.h		\
	gstidentity.h		\
	gsttee.h		\
	gsttypefindelement.h

EXTRA_DIST = gstfdsrc.c
