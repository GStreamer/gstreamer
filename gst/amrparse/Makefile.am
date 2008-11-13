plugin_LTLIBRARIES = libgstamrparse.la

libgstamrparse_la_SOURCES = \
	gstamrparse.c gstbaseparse.c

libgstamrparse_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstamrparse_la_LIBADD = $(GST_BASE_LIBS)
libgstamrparse_la_LDFLAGS = $(PACKAGE_LIBS) $(GST_PLUGIN_LDFLAGS)
libgstamrparse_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstamrparse.h gstbaseparse.h
