## Process this file with automake to produce Makefile.in

INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir) \
	           $(shell gnome-config --cflags gnomeui)


bin_PROGRAMS = gstplay

gstplay_SOURCES = \
	gstplay.c \
	mpeg1.c mpeg2.c avi.c\
	support.c support.h \
	interface.c interface.h \
	callbacks.c callbacks.h

noinst_HEADERS = codecs.h

CFLAGS += -O2 -Wall

gstplay_CFLAGS = $(shell gnome-config --cflags gnomeui)
gstplay_LDFLAGS = $(shell gnome-config --libs gnomeui)

LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_builddir)/gst/libgst.la


