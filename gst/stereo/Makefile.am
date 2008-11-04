
plugin_LTLIBRARIES = libgststereo.la

libgststereo_la_SOURCES = gststereo.c
libgststereo_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CONTROLLER_CFLAGS)
libgststereo_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(GST_CONTROLLER_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR)
libgststereo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgststereo_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gststereo.h

EXTRA_DIST =
