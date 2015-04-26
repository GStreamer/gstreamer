
plugin_LTLIBRARIES = libgsty4mdec.la

libgsty4mdec_la_SOURCES = gsty4mdec.c
libgsty4mdec_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgsty4mdec_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgsty4mdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsty4mdec_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gsty4mdec.h
