
plugin_LTLIBRARIES = libgsty4mdec.la

libgsty4mdec_la_SOURCES = gsty4mdec.c
libgsty4mdec_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgsty4mdec_la_LIBADD = \
	$(GST_BASE_PLUGINS_LIBS) -lgstvideo-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) -lgstbase-@GST_MAJORMINOR@ \
	$(GST_LIBS)
libgsty4mdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsty4mdec_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gsty4mdec.h
