## Process this file with automake to produce Makefile.in

INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir) \
	           $(shell gnome-config --cflags gnomeui) -I..


bin_PROGRAMS = gstmediaplay 

lib_LTLIBRARIES = libgstmediaplay.la

gladedir = $(datadir)/gstmediaplay
glade_DATA = gstmediaplay.glade play.xpm stop.xpm pause.xpm

libgstmediaplay_la_SOURCES = \
	gstplay.c \
	gstmediaplay.c gstmediaplay.h \
	gststatusarea.c gststatusarea.h \
	callbacks.c callbacks.h 

gstmediaplay_SOURCES = \
	main.c 


CFLAGS += -O2 -Wall -DDATADIR=\""$(gladedir)/"\"
#CFLAGS += -pg -O2 -Wall -DDATADIR=\""$(gladedir)/"\"

gstmediaplay_CFLAGS = $(shell gnome-config --cflags gnomeui) $(shell libglade-config --cflags gnome) \
		 -I..

gstmediaplay_LDFLAGS = $(shell gnome-config --libs gnomeui) $(shell libglade-config --libs gnome) \
		 ../gst/libgst.la ./libgstmediaplay.la


if HAVE_LIBXV
xvlibs=-lXv
else
xvlibs=
endif

gstmediaplay_LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_builddir)/gst/libgst.la \
          $(top_builddir)/plugins/videosink/gdkxvimage.lo ${xvlibs} -lXxf86vm


