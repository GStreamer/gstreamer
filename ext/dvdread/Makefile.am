plugin_LTLIBRARIES = libgstdvdread.la

libgstdvdread_la_SOURCES = dvdreadsrc.c
libgstdvdread_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(DVDREAD_CFLAGS)
libgstdvdread_la_LIBADD = $(GST_BASE_LIBS) $(DVDREAD_LIBS)
libgstdvdread_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdvdread_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = dvdreadsrc.h

EXTRA_DIST = README demo-play
