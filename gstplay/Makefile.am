## Process this file with automake to produce Makefile.in

# FIXME FIXME


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
	callbacks.c

gstmediaplay_SOURCES = \
	main.c 

libgstmediaplayincludedir = $(includedir)/gstplay
libgstmediaplayinclude_HEADERS = \
	gstplay.h 

noinst_HEADERS = \
	gstmediaplay.h \
	gststatusarea.h \
	callbacks.h

libgstmediaplay_la_CFLAGS = $(GST_CFLAGS) $(GNOME_CFLAGS) \
	-DDATADIR=\""$(gladedir)/"\" $(LIBGLADE_CFLAGS)
libgstmediaplay_la_LIBADD = $(GST_LIBS) $(GNOME_LIBS) $(LIBGLADE_LIBS)
libgstmediaplay_la_LDFLAGS = -rdynamic

gstmediaplay_CFLAGS = $(GST_CFLAGS) $(LIBGLADE_CFLAGS) $(GNOME_CFLAGS)
gstmediaplay_LDADD = $(GST_LIBS) $(LIBGLADE_LIBS) $(GNOME_LIBS) libgstmediaplay.la
