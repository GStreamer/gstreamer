# FIXME:
# need to get gstbufferstore.[ch] into its own lib, preferrably 
# libs/gst/buifferstore
# This requires building libs/gst before this dir, which we currently don't
# do.

plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstelements.la

# FIXME:
# Disable multifilesrc on Windows, cause it uses mmap excessively
# and I don't feel like fixing it yet.  See also the disablement
# in gstelements.c.
if AS_LIBTOOL_WIN32
multifilesrc = 
else
multifilesrc = gstmultifilesrc.c
endif

libgstelements_la_DEPENDENCIES = ../libgstreamer-@GST_MAJORMINOR@.la
libgstelements_la_SOURCES =	\
	gstaggregator.c		\
	gstbufferstore.c	\
	gstelements.c		\
	gstfakesink.c		\
	gstfakesrc.c		\
	gstfilesink.c		\
	gstfilesrc.c		\
	gstfdsink.c		\
	gstfdsrc.c		\
	gstidentity.c		\
	gstmd5sink.c		\
	$(multifilesrc)		\
	gstpipefilter.c		\
	gstshaper.c		\
	gststatistics.c		\
	gsttee.c		\
	gsttypefind.c

libgstelements_la_CFLAGS = $(GST_CFLAGS)
libgstelements_la_LIBADD =
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
	gsttypefind.h
