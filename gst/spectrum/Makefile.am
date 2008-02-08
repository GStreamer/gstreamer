
plugin_LTLIBRARIES = libgstspectrum.la

libgstspectrum_la_SOURCES = gstspectrum.c
libgstspectrum_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstspectrum_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) -lgstaudio-$(GST_MAJORMINOR) -lgstfft-$(GST_MAJORMINOR) $(LIBM)
libgstspectrum_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstspectrum.h

EXTRA_DIST = README
