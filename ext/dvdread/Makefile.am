
plugin_LTLIBRARIES = libgstdvdreadsrc.la

## FIXME: the extra option, what do we do with it ?
## AM_CFLAGS = -D_LARGEFILE64_SOURCE $(LIBDVDREADCFLAGS)

libgstdvdreadsrc_la_SOURCES = dvdreadsrc.c
libgstdvdreadsrc_la_CFLAGS = $(GST_CFLAGS)
libgstdvdreadsrc_la_LIBADD = $(DVDREAD_LIBS)
libgstdvdreadsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = dvdreadsrc.h

EXTRA_DIST = \
	README \
	demo-play
