lib_LTLIBRARIES = libgstelements.la

libgstelements_la_DEPENDENCIES = ../libgst.la
libgstelements_la_SOURCES =	\
	gstelements.c		\
	gstfakesrc.c		\
	gstidentity.c		\
	gstfakesink.c		\
	gstdisksrc.c		\
	gstasyncdisksrc.c	\
	gstfdsrc.c		\
	gsthttpsrc.c		\
	gstaudiosink.c		\
	gstaudiosrc.c		\
	gstfdsink.c		\
	gstqueue.c		\
	gstsinesrc.c

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
	gstqueue.h		\
	gstsinesrc.h

libgstelements_la_LIBADD = $(GLIB_LIBS) $(GTK_LIBS) 
libgstelements_la_LDFLAGS = -version-info $(STREAMER_CURRENT):$(STREAMER_REVISION):$(STREAMER_AGE)
