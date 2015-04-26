plugin_LTLIBRARIES = libgstliveadder.la

libgstliveadder_la_SOURCES = liveadder.c
libgstliveadder_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstliveadder_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstliveadder_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstliveadder_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = liveadder.h
