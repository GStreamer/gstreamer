
plugin_LTLIBRARIES = libgstalsa.la

libgstalsa_la_SOURCES = gstalsaplugin.c \
  gstalsa.c gstalsaclock.c gstalsasink.c gstalsasrc.c \
  gstalsamixer.c gstalsamixertrack.c

libgstalsa_la_CFLAGS = $(GST_CFLAGS) $(ALSA_CFLAGS)
libgstalsa_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstalsa_la_LIBADD = $(ALSA_LIBS) \
  ${top_builddir}/gst-libs/gst/libgstinterfaces-@GST_MAJORMINOR@.la

noinst_HEADERS = \
  gstalsa.h gstalsaclock.h gstalsasink.h gstalsasrc.h \
  gstalsamixer.h gstalsamixertrack.h
