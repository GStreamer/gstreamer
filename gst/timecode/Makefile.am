plugin_LTLIBRARIES = libgsttimecode.la

libgsttimecode_la_SOURCES = plugin.c gsttimecodestamper.c gstavwait.c

libgsttimecode_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgsttimecode_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) -lgstaudio-$(GST_API_VERSION) $(GST_BASE_LIBS) $(GST_LIBS) $(LIBM)
libgsttimecode_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttimecode_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gsttimecodestamper.h gstavwait.h
