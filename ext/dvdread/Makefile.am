plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libdvdsrc.la

## FIXME: the extra option, what do we do with it ?
## AM_CFLAGS = -D_LARGEFILE64_SOURCE $(LIBDVDREADCFLAGS)

libdvdsrc_la_SOURCES = dvdsrc.c
libdvdsrc_la_LDFLAGS = $(DVDREAD_LIBS)
libdvdsrc_la_CFLAGS = $(GST_CFLAGS)

noinst_HEADERS = dvdsrc.h

if HAVE_GTK
noinst_PROGRAMS = dvdplay
endif

dvdplay_SOURCES = dvdplay.c
dvdplay_CFLAGS  = $(GST_CFLAGS) $(GTK_CFLAGS)
dvdplay_LDFLAGS = $(GST_LIBS) $(GTK_LIBS)

EXTRA_DIST = README
