# FIXME:
# need to get gstbufferstore.[ch] into its own lib, preferrably 
# libs/gst/buifferstore
# This requires building libs/gst before this dir, which we currently don't
# do.

plugin_LTLIBRARIES = libgstelements.la

libgstelements_la_DEPENDENCIES = ../libgstreamer-@GST_MAJORMINOR@.la
libgstelements_la_SOURCES =	\
	gstbufferstore.c	\
	gstcapsfilter.c		\
	gstfakesrc.c		\
	gstfakesink.c		\
	gstfdsrc.c		\
	gstfilesink.c		\
	gstfilesrc.c		\
	gstidentity.c		\
	gstelements.c		\
	gsttee.c		\
	gsttypefindelement.c

libgstelements_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstelements_la_LIBADD = $(GST_OBJ_LIBS) \
	$(top_builddir)/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
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

