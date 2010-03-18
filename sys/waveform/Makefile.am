plugin_LTLIBRARIES = libgstwaveformsink.la

# FIXME: Replace DIRECTSOUND CFLAGS+LIBS with waveform related ones and fix
# the configure.ac + sys/Makefile.am to get this stuff building in MingW
# For now, it's just disted for use in the VS builds.

libgstwaveformsink_la_SOURCES =  gstwaveformsink.c gstwaveformplugin.c
libgstwaveformsink_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
        $(GST_PLUGINS_BASE_CFLAGS) $(DIRECTSOUND_CFLAGS)
libgstwaveformsink_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
        -lgstaudio-$(GST_MAJORMINOR) -lgstinterfaces-$(GST_MAJORMINOR) \
        $(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(DIRECTSOUND_LIBS)
libgstwaveformsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(DIRECTSOUND_LDFLAGS)
libgstwaveformsink_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstwaveformsink.h

