filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstelements.la

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

libgstelements_la_CFLAGS = $(GHTTP_CFLAGS) $(GST_CFLAGS)
libgstelements_la_LIBADD = $(GHTTP_LIBS) $(GST_LIBS)
## left from merging, FIXME or DELETEME libgstelements_la_LDFLAGS = -version-info $(GST_LIBVERSION)
