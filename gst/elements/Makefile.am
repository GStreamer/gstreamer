# FIXME:
# need to get gstbufferstore.[ch] into its own lib, preferrably 
# libs/gst/buifferstore
# This requires building libs/gst before this dir, which we currently don't
# do.

plugin_LTLIBRARIES = libgstelements.la
AS_LIBTOOL_LIB = libgstelements

EXTRA_DIST = $(as_libtool_EXTRA_DIST)
noinst_DATA = $(as_libtool_noinst_DATA_files)

# FIXME:
# Disable multifilesrc on Windows, cause it uses mmap excessively
# and I don't feel like fixing it yet.  See also the disablement
# in gstelements.c.
if AS_LIBTOOL_WIN32
multifilesrc = 
pipefilter =
else
multifilesrc = gstmultifilesrc.c
pipefilter = gstpipefilter.c
endif

libgstelements_la_DEPENDENCIES = ../libgstreamer-@GST_MAJORMINOR@.la
libgstelements_la_SOURCES =	\
	gstfakesrc.c		\
	gstfakesink.c		\
	gstfilesrc.c		\
	gstidentity.c		\
	gstelements.c		\
	gsttee.c

# FIXME 0.9: mentioned by po/POTFILES.in, so we include them here --
# this is a short-term hack, really we should just decide the fate of
# these files above, then this will be unnecessary
EXTRA_DIST +=			\
	gstaggregator.c		\
	gstbufferstore.c	\
	gstfilesink.c		\
	gstfdsink.c		\
	gstfdsrc.c		\
	gstmd5sink.c		\
	gstmultifilesrc.c	\
	gstpipefilter.c		\
	gstshaper.c		\
	gststatistics.c		\
	gsttypefindelement.c


libgstelements_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstelements_la_LIBADD = $(GST_OBJ_LIBS)
libgstelements_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(as_libtool_LDFLAGS) \
	 $(top_builddir)/gst/base/libgstbase-@GST_MAJORMINOR@.la

noinst_HEADERS =		\
	gstaggregator.h		\
	gstbufferstore.h	\
	gstfakesink.h		\
	gstfakesrc.h		\
	gstfdsink.h		\
	gstfdsrc.h		\
	gstfilesink.h		\
	gstfilesrc.h		\
	gstidentity.h		\
	gstmd5sink.h		\
	gstmultifilesrc.h	\
	gstpipefilter.h		\
	gstshaper.h		\
	gststatistics.h		\
	gsttee.h		\
	gsttypefindelement.h

install-data-local: as-libtool-install-data-local

uninstall-local: as-libtool-uninstall-local

include $(top_srcdir)/common/as-libtool.mak

