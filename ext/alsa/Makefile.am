plugin_LTLIBRARIES = libgstalsa.la

libgstalsa_la_SOURCES = \
	gstalsadeviceprobe.c \
	gstalsaplugin.c \
	gstalsasink.c 	\
	gstalsasrc.c \
	gstalsa.c

libgstalsa_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(ALSA_CFLAGS)
libgstalsa_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) \
	$(ALSA_LIBS)

libgstalsa_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstalsa_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstalsa.h \
	gstalsadeviceprobe.h \
	gstalsasrc.h \
	gstalsasink.h
