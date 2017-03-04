plugin_LTLIBRARIES = libgstsoup.la

libgstsoup_la_SOURCES = gstsouphttpsrc.c gstsouphttpclientsink.c gstsouputils.c gstsoup.c

libgstsoup_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(SOUP_CFLAGS) \
	-DSOUP_VERSION_MIN_REQUIRED=SOUP_VERSION_2_48 \
	-DSOUP_VERSION_MAX_ALLOWED=SOUP_DEPRECATED_IN_2_48
libgstsoup_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_API_VERSION@ $(GST_BASE_LIBS) $(SOUP_LIBS)
libgstsoup_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsoup_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstsouphttpsrc.h gstsouphttpclientsink.h gstsouputils.h
