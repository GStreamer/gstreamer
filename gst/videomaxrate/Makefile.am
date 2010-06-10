plugin_LTLIBRARIES = libgstvideomaxrate.la

libgstvideomaxrate_la_SOURCES = videomaxrate.c

noinst_HEADERS = videomaxrate.h
libgstvideomaxrate_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstvideomaxrate_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstvideomaxrate_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideomaxrate_la_LIBTOOLFLAGS = --tag=disable-static

