plugindir = $(libdir)/gst

man_MANS = gstalsa.1

plugin_LTLIBRARIES = libgstalsa.la

libgstalsa_la_SOURCES = gstalsa.c
libgstalsa_la_CFLAGS = $(GST_CFLAGS) $(ALSA_CFLAGS)
libgstalsa_la_LIBADD = $(ALSA_LIBS)
libgstalsa_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstalsa.h

EXTRA_DIST = README $(man_MANS)
