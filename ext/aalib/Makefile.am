plugin_LTLIBRARIES = libgstaasink.la

libgstaasink_la_SOURCES = gstaasink.c
libgstaasink_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(AALIB_CFLAGS)
libgstaasink_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(GST_BASE_LIBS) $(GST_LIBS) $(AALIB_LIBS)
libgstaasink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaasink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstaasink.h
