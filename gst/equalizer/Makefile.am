plugin_LTLIBRARIES = libgstequalizer.la

libgstequalizer_la_SOURCES = \
        gstiirequalizer.c gstiirequalizer.h \
        gstiirequalizernbands.c gstiirequalizernbands.h \
        gstiirequalizer3bands.c gstiirequalizer3bands.h \
        gstiirequalizer10bands.c gstiirequalizer10bands.h

libgstequalizer_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CONTROLLER_CFLAGS) $(GST_CFLAGS)
libgstequalizer_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-$(GST_MAJORMINOR) $(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS) \
	$(GST_LIBS) $(LIBM)
libgstequalizer_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstequalizer_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstiirequalizer.h

presetdir = $(datadir)/gstreamer-$(GST_MAJORMINOR)/presets
preset_DATA = GstIirEqualizer3Bands.prs GstIirEqualizer10Bands.prs

EXTRA_DIST = $(preset_DATA)

