plugin_LTLIBRARIES = libgstaudiobuffersplit.la

libgstaudiobuffersplit_la_SOURCES = gstaudiobuffersplit.c

libgstaudiobuffersplit_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstaudiobuffersplit_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS)  $(GST_LIBS) $(LIBM)
libgstaudiobuffersplit_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstaudiobuffersplit_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstaudiobuffersplit.h
