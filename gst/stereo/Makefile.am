
plugin_LTLIBRARIES = libgststereo.la

libgststereo_la_SOURCES = gststereo.c
libgststereo_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS)
libgststereo_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION)
libgststereo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgststereo_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gststereo.h
