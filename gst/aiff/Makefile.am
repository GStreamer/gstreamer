plugin_LTLIBRARIES = libgstaiff.la

noinst_HEADERS = aiffmux.h aiffparse.h

libgstaiff_la_SOURCES = aiff.c aiffmux.c aiffparse.c
libgstaiff_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstaiff_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) -lgstaudio-$(GST_API_VERSION) -lgstpbutils-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(LIBM)
libgstaiff_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaiff_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
