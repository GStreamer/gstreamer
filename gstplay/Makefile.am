## Process this file with automake to produce Makefile.in

# FIXME FIXME

LIBADD += $(GNOME_LIBS) $(GST_LIBS)
CFLAGS += $(GNOME_CFLAGS) $(GST_CFLAGS) -DDATADIR=\""$(gladedir)/"\"

bin_PROGRAMS = gstmediaplay 

lib_LTLIBRARIES = libgstmediaplay.la

man_MANS = gstmediaplay.1

gladedir = $(datadir)/gstmediaplay
glade_DATA = gstmediaplay.glade play.xpm stop.xpm pause.xpm

EXTRA_DIST = $(glade_DATA) $(man_MANS)

libgstmediaplay_la_SOURCES = \
	gstplay.c \
	gstmediaplay.c \
	gststatusarea.c \
	callbacks.c \
	full-screen.c

gstmediaplay_SOURCES = \
	main.c 

libgstmediaplayincludedir = $(includedir)/gstplay
libgstmediaplayinclude_HEADERS = \
	gstplay.h 

noinst_HEADERS = \
	gstmediaplay.h \
	gststatusarea.h \
	callbacks.h \
	gstplayprivate.h \
	full-screen.h


libgstmediaplay_la_LDFLAGS = -rdynamic

gstmediaplay_CFLAGS = $(LIBGLADE_GNOME_CFLAGS)
gstmediaplay_LDADD = $(GST_LIBS) $(LIBGLADE_GNOME_LIBS) libgstmediaplay.la
