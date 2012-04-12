plugin_LTLIBRARIES = libgstsndio.la

libgstsndio_la_SOURCES = gstsndio.c sndiosink.c sndiosrc.c 
libgstsndio_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstsndio_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) \
	$(SNDIO_LIBS)
libgstsndio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = sndiosink.h sndiosrc.h
EXTRA_DIST =
