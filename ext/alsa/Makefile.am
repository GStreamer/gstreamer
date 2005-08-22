plugin_LTLIBRARIES = libgstalsa.la

libgstalsa_la_SOURCES = \
	gstalsaplugin.c \
	gstalsamixer.c	\
	gstalsamixerelement.c \
	gstalsamixertrack.c \
	gstalsamixeroptions.c \
	gstalsasink.c 	\
	gstalsasrc.c

libgstalsa_la_CFLAGS = $(GST_CFLAGS) $(ALSA_CFLAGS)
libgstalsa_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstalsa_la_LIBADD = \
	$(ALSA_LIBS) \
  $(top_builddir)/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la 	\
  $(top_builddir)/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la

noinst_HEADERS = \
	gstalsa.h \
	gstalsasink.h \
	gstalsasrc.h \
	gstalsamixer.h \
	gstalsamixerelement.h \
	gstalsamixertrack.h \
	gstalsamixeroptions.h
