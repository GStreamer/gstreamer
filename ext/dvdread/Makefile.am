plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstdvdsrc.la

## FIXME: the extra option, what do we do with it ?
## AM_CFLAGS = -D_LARGEFILE64_SOURCE $(LIBDVDREADCFLAGS)

libgstdvdsrc_la_SOURCES = dvdsrc.c
libgstdvdsrc_la_CFLAGS = $(GST_CFLAGS)
libgstdvdsrc_la_LIBADD = $(GST_LIBS) $(DVDREAD_LIBS)
libgstdvdsrc_la_LDFLAGS = @GST_PLUGIN_LDFLAGS@

noinst_HEADERS = dvdsrc.h

# fix this

#noinst_PROGRAMS = demo-play
#
#demo_play_SOURCES = demo-play.c
#demo_play_CFLAGS  = $(GST_CFLAGS) $(GNOME_CFLAGS)
#demo_play_LDFLAGS = $(GST_LIBS) $(GNOME_LIBS)

EXTRA_DIST = demo-play.c README
