LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_builddir)/gst/libgst.la 
LDFLAGS = $(shell gnome-config --libs gnomeui)  $(shell libglade-config --libs gnome)
INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir)/gst $(shell libglade-config --cflags gnome) \
           $(shell gnome-config --cflags gnomeui)


lib_LTLIBRARIES = libgsteditor.la

libgsteditor_la_SOURCES =	\
	gsteditor.c		\
	gsteditorproject.c	\
	gsteditorprojectview.c	\
	gsteditorelement.c	\
	gsteditorbin.c		\
	gsteditorcanvas.c	\
	gsteditorpad.c		\
	gsteditorpadtemplate.c	\
	gsteditorconnection.c	\
	gsteditorimage.c	\
	gsteditorpalette.c	\
	gsteditorproperty.c	\
	gstelementselect.c	\
	gsteditorcreate.c

libgsteditorincludedir = $(includedir)/gst
libgsteditorinclude_HEADERS =	\
	gsteditor.h


bin_PROGRAMS = gsteditor
gsteditor_SOURCES = editor.c
gsteditor_LDFLAGS = libgsteditor.la


noinst_HEADERS =		\
	gsteditorproject.h	\
	gstelementselect.h	\
	gsteditorpalette.h	\
	gsteditorproperty.h	\
	gsteditorimage.h	\
	gsteditorcreate.h

CFLAGS = -O2 -Wall

EXTRA_DIST = editor.glade editorelement.glade
