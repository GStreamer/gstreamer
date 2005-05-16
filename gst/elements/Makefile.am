# FIXME:
# need to get gstbufferstore.[ch] into its own lib, preferrably 
# libs/gst/buifferstore
# This requires building libs/gst before this dir, which we currently don't
# do.

plugin_LTLIBRARIES = libgstelements.la
AS_LIBTOOL_LIB = libgstelements

EXTRA_DIST = $(as_libtool_EXTRA_DIST)
noinst_DATA = $(as_libtool_noinst_DATA_files)

libgstelements_la_DEPENDENCIES = ../libgstreamer-@GST_MAJORMINOR@.la
libgstelements_la_SOURCES =	\
	gstbufferstore.c	\
	gstelements.c		\
	gstfakesink.c		\
	gstfakesrc.c		\
	gstfilesink.c		\
	gstfilesrc.c		\
	gstfdsink.c		\
	gstfdsrc.c		\
	gstidentity.c		\
	gsttee.c		\
	gsttypefindelement.c

libgstelements_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstelements_la_LIBADD = $(GST_OBJ_LIBS)
libgstelements_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(as_libtool_LDFLAGS)

noinst_HEADERS =		\
	gstbufferstore.h	\
	gstfakesink.h		\
	gstfakesrc.h		\
	gstfdsink.h		\
	gstfdsrc.h		\
	gstfilesink.h		\
	gstfilesrc.h		\
	gstidentity.h		\
	gsttee.h		\
	gsttypefindelement.h

install-data-local: as-libtool-install-data-local

uninstall-local: as-libtool-uninstall-local

include $(top_srcdir)/common/as-libtool.mak

