# FIXME FIXME

Gamesdir = $(datadir)/gnome/apps/Games

INCLUDES = -I$(top_srcdir)/gst				\
	-I$(top_srcdir)/gstplay				\
	-DGNOMELOCALEDIR=\""$(datadir)/locale"\" 	\
	-DDATADIR=\""$(datadir)"\"			\
	-I$(includedir)				 	\
	$(shell gnome-config --cflags gnome gnomeui bonobo bonobox) \
	$(BONOBOX_TEST_CFLAGS) -Wall -O2

bin_PROGRAMS = bonobo-gstmediaplay

bonobo_gstmediaplay_SOURCES = 	\
	bonobo-gstmediaplay.c

bonobo_gstmediaplay_CFLAGS = -Wall -O2		\
	$(shell gnome-config --cflags gnome gnomeui bonobo bonobox)  $(shell libglade-config --cflags gnome) \
	$(shell gstreamer-config --clfags )

bonobo_gstmediaplay_LDADD =		\
	$(top_srcdir)/gstplay/libgstmediaplay.la \
	$(top_srcdir)/gst/libgstreamer.la

bonobo_gstmediaplay_LDFLAGS =		\
	$(shell gnome-config --libs gnome gnomeui bonobo bonobox)  $(shell libglade-config --libs gnome) \
	$(shell gstreamer-config --libs ) 

oafdir = $(datadir)/oaf
OAF_FILES = gstmediaplay.oafinfo
oaf_DATA = $(OAF_FILES)

EXTRA_DIST = gstmediaplay.desktop $(OAF_FILES) \
	bonobo-gstmediaplay-ui.xml
