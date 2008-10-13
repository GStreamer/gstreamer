plugin_LTLIBRARIES = libgstdvdnav.la

libgstdvdnav_la_SOURCES = dvdnavsrc.c
libgstdvdnav_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(DVDNAV_CFLAGS)
libgstdvdnav_la_LIBADD = $(GST_BASE_LIBS) $(DVDNAV_LIBS)
libgstdvdnav_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdvdnav_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = dvdnavsrc.h

EXTRA_DIST = README
