## Process this file with automake to produce Makefile.in

INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir) \
	           $(shell gnome-config --cflags gnomeui) $(shell gstreamer-config --cflags)


bin_PROGRAMS = gstplay

gladedir = $(datadir)/gstplay
glade_DATA = gstplay.glade play.xpm stop.xpm pause.xpm

gstplay_SOURCES = \
	gstplay.c \
	interface.c interface.h \
	callbacks.c callbacks.h

CFLAGS += -O2 -Wall -DDATADIR=\""$(gladedir)/"\"

gstplay_CFLAGS = $(shell gnome-config --cflags gnomeui) $(shell libglade-config --cflags gnome) \
		 $(shell gstreamer-config --cflags ) 
gstplay_LDFLAGS = $(shell gnome-config --libs gnomeui) $(shell libglade-config --libs gnome) \
		 $(shell gstreamer-config --libs ) 

if HAVE_LIBXV
xvlibs=-lXv
else
xvlibs=
endif

gstplay_LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_builddir)/gst/libgst.la \
          $(top_builddir)/plugins/videosink/gdkxvimage.lo ${xvlibs} -lXxf86vm


