## Process this file with automake to produce Makefile.in

# FIXME FIXME

LIBADD += $(GNOME_LIBS) $(GST_LIBS)
CFLAGS += $(GNOME_CFLAGS) $(GST_CFLAGS) -DDATADIR=\""$(gladedir)/"\"

bin_PROGRAMS = gstmediaplay 

lib_LTLIBRARIES = libgstmediaplay.la

gladedir = $(datadir)/gstmediaplay
glade_DATA = gstmediaplay.glade play.xpm stop.xpm pause.xpm

EXTRA_DIST = $(glade_DATA)

libgstmediaplay_la_SOURCES = \
	gstplay.c \
	gstmediaplay.c \
	gststatusarea.c \
	callbacks.c

gstmediaplay_SOURCES = \
	main.c 

noinst_HEADERS = \
	gstmediaplay.h \
	gststatusarea.h \
	callbacks.h \
	gstplay.h \
	gstplayprivate.h


gstmediaplay_CFLAGS = $(LIBGLADE_GNOME_CFLAGS)
gstmediaplay_LDADD = $(GST_LIBS) $(LIBGLADE_GNOME_LIBS) libgstmediaplay.la
