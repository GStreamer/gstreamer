plugin_LTLIBRARIES = libgstaacparse.la

libgstaacparse_la_SOURCES = \
	gstaacparse.c gstbaseparse.c

libgstaacparse_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstaacparse_la_LIBADD = $(GST_BASE_LIBS)
libgstaacparse_la_LDFLAGS = $(PACKAGE_LIBS) $(GST_PLUGIN_LDFLAGS)
libgstaacparse_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstaacparse.h gstbaseparse.h
