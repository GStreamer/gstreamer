filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstelements.la

if USE_LIBGHTTP
GSTHTTPSRC=gsthttpsrc.c
else
GSTHTTPSRC=
endif

libgstelements_la_DEPENDENCIES = ../libgst.la
libgstelements_la_SOURCES =	\
	gstelements.c		\
	gstfakesrc.c		\
	gstidentity.c		\
	gstfakesink.c		\
	gstdisksrc.c		\
	gstfilesrc.c		\
	gstdisksink.c		\
	gstfdsrc.c		\
	gstfdsink.c		\
	gstmultidisksrc.c	\
	gstpipefilter.c		\
	gsttee.c		\
	gstaggregator.c		\
	gststatistics.c		\
	$(GSTHTTPSRC)

noinst_HEADERS =		\
	gstfakesrc.h		\
	gstidentity.h		\
	gstfakesink.h		\
	gstdisksrc.h		\
	gstdisksink.h		\
	gstfdsrc.h		\
	gstmultidisksrc.h	\
	gsthttpsrc.h		\
	gstfdsink.h		\
	gstpipefilter.h		\
	gsttee.h		\
	gstaggregator.h		\
	gststatistics.h	        \
	gstfilesrc.h

libgstelements_la_CFLAGS = $(GHTTP_CFLAGS) $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)
libgstelements_la_LIBADD = $(GHTTP_LIBS) $(GLIB_LIBS) $(XML_LIBS)
libgstelements_la_LDFLAGS = -version-info $(GST_LIBVERSION)
