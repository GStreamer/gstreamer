LDFLAGS = $(GLIB_LIBS) $(GTK_LIBS) $(top_srcdir)/gst/libgst.la \
	$(shell gnome-config --libs gnomeui)
INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir)/gst \
           $(shell gnome-config --cflags gnomeui)


lib_LTLIBRARIES = libgsteditor.la

libgsteditor_la_SOURCES =	\
	gsteditor.c		\
	gsteditorelement.c	\
	gsteditorbin.c		\
	gsteditorcanvas.c	\
	gsteditorpad.c		\
	gsteditorconnection.c	\
	gstelementselect.c	\
	gsteditorcreate.c

libgsteditorincludedir = $(includedir)/gst
libgsteditorinclude_HEADERS =	\
	gsteditor.h


bin_PROGRAMS = editor
editor_LDFLAGS = libgsteditor.la


noinst_HEADERS =		\
	gstelementselect.h	\
	gsteditorcreate.h

EXTRA_DIST = editor.glade editorelement.glade
