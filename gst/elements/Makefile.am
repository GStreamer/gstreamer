# FIXME:
# need to get gstbufferstore.[ch] into its own lib, preferrably 
# libs/gst/buifferstore
# This requires building libs/gst before this dir, which we currently don't
# do.

plugin_LTLIBRARIES = libgstelements.la

multifilesrc = gstmultifilesrc.c
pipefilter = gstpipefilter.c

libgstelements_la_DEPENDENCIES = ../libgstreamer-@GST_MAJORMINOR@.la
libgstelements_la_SOURCES =	\
	gstbufferstore.c	\
	gstcapsfilter.c		\
	gstfakesrc.c		\
	gstfakesink.c		\
	gstfilesink.c		\
	gstfilesrc.c		\
	gstidentity.c		\
	gstelements.c		\
	gsttee.c		\
	gsttypefindelement.c

# FIXME 0.9: mentioned by po/POTFILES.in, so we include them here --
# this is a short-term hack, really we should just decide the fate of
# these files above, then this will be unnecessary
EXTRA_DIST =			\
	gstaggregator.c		\
	gstcapsfilter.c		\
	gstfdsink.c		\
	gstfdsrc.c		\
	gstmd5sink.c		\
	gstmultifilesrc.c	\
	gstpipefilter.c		\
	gstshaper.c		\
	gststatistics.c


libgstelements_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstelements_la_LIBADD = $(GST_OBJ_LIBS) \
	$(top_builddir)/gst/base/libgstbase-@GST_MAJORMINOR@.la \
	$(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
libgstelements_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

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

