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
libgsteditor_la_LIBADD = $(GNOME_LIBS) $(LIBGLADE_GNOME_LIBS)

libgsteditorincludedir = $(includedir)/gst
libgsteditorinclude_HEADERS =	\
	gsteditor.h

bin_PROGRAMS = gsteditor
gsteditor_SOURCES = editor.c

gsteditor_CFLAGS = $(GST_CFLAGS) $(GNOME_CFLAGS) $(LIBGLADE_GNOME_CFLAGS)
gsteditor_LDADD = $(GST_LIBS) $(GNOME_LIBS) $(LIBGLADE_GNOME_LIBS) libgsteditor.la

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
