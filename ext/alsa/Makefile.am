
plugin_LTLIBRARIES = libgstalsa.la

libgstalsa_la_SOURCES = gstalsaplugin.c \
  gstalsa.c gstalsaclock.c gstalsasink.c gstalsasrc.c

libgstalsa_la_CFLAGS = $(GST_CFLAGS) $(ALSA_CFLAGS)
libgstalsa_la_LIBADD = $(ALSA_LIBS)
libgstalsa_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
  gstalsa.h gstalsaclock.h gstalsasink.h gstalsasrc.h

EXTRA_DIST = README
