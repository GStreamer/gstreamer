plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstalsa.la

libgstalsa_la_SOURCES = gstalsa.c
libgstalsa_la_CFLAGS = $(GST_CFLAGS) $(ALSA_CFLAGS)
libgstalsa_la_LIBADD = $(GST_LIBS) $(ALSA_LIBS)
libgstalsa_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstalsa.h

EXTRA_DIST = README
