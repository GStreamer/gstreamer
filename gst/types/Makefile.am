filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgsttypes.la

libgsttypes_la_DEPENDENCIES = ../libgst.la
libgsttypes_la_SOURCES =	\
	gsttypes.c

#noinst_HEADERS =

libgsttypes_la_LIBADD = $(GLIB_LIBS) $(GTK_LIBS) 
libgsttypes_la_LDFLAGS = -version-info $(STREAMER_CURRENT):$(STREAMER_REVISION):$(STREAMER_AGE)
