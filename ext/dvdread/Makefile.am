plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstdvdreadsrc.la

## FIXME: the extra option, what do we do with it ?
## AM_CFLAGS = -D_LARGEFILE64_SOURCE $(LIBDVDREADCFLAGS)

libgstdvdreadsrc_la_SOURCES = dvdreadsrc.c
libgstdvdreadsrc_la_CFLAGS = $(GST_CFLAGS)
libgstdvdreadsrc_la_LIBADD = $(DVDREAD_LIBS)
libgstdvdreadsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = dvdreadsrc.h

# fix this

#noinst_PROGRAMS = demo-play
#
#demo_play_SOURCES = demo-play.c
#demo_play_CFLAGS  = $(GST_CFLAGS) $(GNOME_CFLAGS)
#demo_play_LDFLAGS = $(GST_LIBS) $(GNOME_LIBS)

EXTRA_DIST = demo-play.c README
