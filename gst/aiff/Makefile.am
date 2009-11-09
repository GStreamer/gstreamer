plugin_LTLIBRARIES = libgstaiff.la

libgstaiff_la_SOURCES = aiff.c aiffparse.c
libgstaiff_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstaiff_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(LIBM)
libgstaiff_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaiff_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = aiffmux.h aiffparse.h
EXTRA_DIST = aiffmux.c
