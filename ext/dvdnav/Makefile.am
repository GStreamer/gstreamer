plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstdvdnavsrc.la

libgstdvdnavsrc_la_SOURCES = dvdnavsrc.c
libgstdvdnavsrc_la_CFLAGS = $(GST_CFLAGS) $(DVDNAV_CFLAGS)
libgstdvdnavsrc_la_LIBADD = $(DVDNAV_LIBS)
libgstdvdnavsrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = dvdnavsrc.h

EXTRA_DIST = README
