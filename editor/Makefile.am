
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

libgsteditor_la_CFLAGS = -DDATADIR="\"$(gladedir)/\"" $(GNOME_CFLAGS) $(LIBGLADE_GNOME_CFLAGS)
libgsteditor_la_LDFLAGS = $(GNOME_LIBS) $(LIBGLADE_GNOME_LIBS)

libgsteditorincludedir = $(includedir)/gst
libgsteditorinclude_HEADERS =	\
	gsteditor.h

bin_PROGRAMS = gsteditor
gsteditor_SOURCES = editor.c
gsteditor_LDFLAGS = libgsteditor.la $(GST_LIBS)

man_MANS = gsteditor.1

gladedir = $(datadir)/gsteditor
glade_DATA = editor.glade editorelement.glade

noinst_HEADERS =		\
	gsteditorproject.h	\
	gstelementselect.h	\
	gsteditorpalette.h	\
	gsteditorproperty.h	\
	gsteditorimage.h	\
	gsteditorcreate.h

SUBDIRS = pixmaps

EXTRA_DIST = $(glade_DATA) $(man_MANS)
