plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libdvdsrc.la

## FIXME: the extra option, what do we do with it ?
## AM_CFLAGS = -D_LARGEFILE64_SOURCE $(LIBDVDREADCFLAGS)

libdvdsrc_la_SOURCES = dvdsrc.c
libdvdsrc_la_LDFLAGS = $(DVDREAD_LIBS)
libdvdsrc_la_CFLAGS = $(GST_CFLAGS)

noinst_HEADERS = dvdsrc.h

# fix this

#noinst_PROGRAMS = demo-play
#
#demo_play_SOURCES = demo-play.c
#demo_play_CFLAGS  = $(GST_CFLAGS) $(GNOME_CFLAGS)
#demo_play_LDFLAGS = $(GST_LIBS) $(GNOME_LIBS)

#EXTRA_DIST = README

EXTRA_DIST = demo-play.c README