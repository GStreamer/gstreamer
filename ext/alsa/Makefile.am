
plugin_LTLIBRARIES = libgstalsa.la

libgstalsa_la_SOURCES = \
	gstalsaplugin.c \
	gstalsasink.c 	\
	gstalsasrc.c

# port alsa stuff then add the _SOURCES above
EXTRA_DIST = \
	gstalsamixer.c \
	gstalsamixertrack.c \
	gstalsamixeroptions.c \
	gstalsa.c \
	gstalsaclock.c 

libgstalsa_la_CFLAGS = $(GST_CFLAGS) $(ALSA_CFLAGS)
libgstalsa_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) \
libgstalsa_la_LIBADD = $(ALSA_LIBS) \
  ${top_builddir}/gst-libs/gst/interfaces/libgstinterfaces-@GST_MAJORMINOR@.la 	\
  ${top_builddir}/gst-libs/gst/audio/libgstaudio-@GST_MAJORMINOR@.la

noinst_HEADERS = \
	gstalsa.h \
	gstalsaclock.h \
	gstalsasink.h \
	gstalsasrc.h \
	gstalsamixer.h \
	gstalsamixertrack.h \
	gstalsamixeroptions.h
