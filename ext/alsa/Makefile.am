plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstalsa.la

libgstalsa_la_SOURCES = gstalsa.c

noinst_HEADERS = gstalsa.h

libgstalsa_la_CFLAGS = $(ALSA_CFLAGS) $(GST_CFLAGS)
libgstalsa_la_LIBADD = $(ALSA_LIBS)

EXTRA_DIST = README

