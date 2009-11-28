plugin_LTLIBRARIES = libgstaudioparsersbad.la

libgstaudioparsersbad_la_SOURCES = \
	gstaacparse.c gstbaseparse.c plugin.c

libgstaudioparsersbad_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstaudioparsersbad_la_LIBADD = $(GST_BASE_LIBS)
libgstaudioparsersbad_la_LDFLAGS = $(PACKAGE_LIBS) $(GST_PLUGIN_LDFLAGS)
libgstaudioparsersbad_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstaacparse.h gstbaseparse.h
