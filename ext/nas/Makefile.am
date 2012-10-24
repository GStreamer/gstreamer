plugin_LTLIBRARIES = libgstnassink.la

libgstnassink_la_SOURCES = nassink.c
libgstnassink_la_CFLAGS = \
	$(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(NAS_CFLAGS)
libgstnassink_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) $(NAS_LIBS)
libgstnassink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstnassink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = nassink.h
