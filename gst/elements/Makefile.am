plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstelements.la

libgstelements_la_DEPENDENCIES = ../libgst.la
libgstelements_la_SOURCES =	\
	gstelements.c		\
	gstfakesrc.c		\
	gstidentity.c		\
	gstfakesink.c		\
	gstfilesrc.c		\
	gstdisksink.c		\
	gstfdsrc.c		\
	gstfdsink.c		\
	gstmultidisksrc.c	\
	gstpipefilter.c		\
	gsttee.c		\
	gstaggregator.c		\
	gststatistics.c
libgstelements_la_CFLAGS = $(GST_CFLAGS)
libgstelements_la_LIBADD = $(GST_LIBS)
libgstelements_la_LDFLAGS = -version-info $(GST_LIBVERSION)

noinst_HEADERS =		\
	gstfakesrc.h		\
	gstidentity.h		\
	gstfakesink.h		\
	gstdisksink.h		\
	gstfdsrc.h		\
	gstmultidisksrc.h	\
	gstfdsink.h		\
	gstpipefilter.h		\
	gsttee.h		\
	gstaggregator.h		\
	gststatistics.h	        \
	gstfilesrc.h
