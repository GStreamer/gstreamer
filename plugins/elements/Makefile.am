filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstelements.la

if HAVE_LIBGHTTP
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
	gstasyncdisksrc.c	\
	gstfdsrc.c		\
	gstaudiosink.c		\
	gstaudiosrc.c		\
	gstfdsink.c		\
	gstpipefilter.c		\
	gstqueue.c		\
	gsttypefind.c		\
	gstsinesrc.c		\
	$(GSTHTTPSRC)

noinst_HEADERS =		\
	gstfakesrc.h		\
	gstidentity.h		\
	gstfakesink.h		\
	gstdisksrc.h		\
	gstasyncdisksrc.h	\
	gstfdsrc.h		\
	gsthttpsrc.h		\
	gstaudiosink.h		\
	gstaudiosrc.h		\
	gstfdsink.h		\
	gstpipefilter.h		\
	gstqueue.h		\
	gsttypefind.h		\
	gstsinesrc.h

CFLAGS += -O2 -Wall

libgstelements_la_LIBADD = $(GLIB_LIBS) $(GTK_LIBS) $(GHTTP_LIBS)
libgstelements_la_LDFLAGS = -version-info $(STREAMER_CURRENT):$(STREAMER_REVISION):$(STREAMER_AGE)
