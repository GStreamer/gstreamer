filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstelements.la

if USE_LIBGHTTP
GSTHTTPSRC=gsthttpsrc.c
else
GSTHTTPSRC=
endif

#CFLAGS += -O2 -Wall -finstrument-functions -DGST_ENABLE_FUNC_INSTRUMENTATION

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
	gstsinesrc.h

CFLAGS += -O2 -Wall 
LDFLAGS += -lm

libgstelements_la_LIBADD = $(GHTTP_LIBS)
libgstelements_la_LDFLAGS = -version-info $(GST_LIBVERSION)
