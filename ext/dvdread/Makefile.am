plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libdvdsrc.la

## FIXME: the extra option, what do we do with it ?
## AM_CFLAGS = -D_LARGEFILE64_SOURCE $(LIBDVDREADCFLAGS)

libdvdsrc_la_SOURCES = dvdsrc.c
libdvdsrc_la_LDFLAGS = $(DVDREAD_LIBS)
libdvdsrc_la_CFLAGS = $(GST_CFLAGS)

noinst_HEADERS = dvdsrc.h

EXTRA_DIST = README
