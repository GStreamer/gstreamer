plugin_LTLIBRARIES = libgstsubenc.la

libgstsubenc_la_SOURCES = \
	gstsrtenc.c
libgstsubenc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GST_CONTROLLER_CFLAGS)
libgstsubenc_la_LIBADD = $(GST_BASE_LIBS) $(GST_CONTROLLER_LIBS)
libgstsubenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsubenc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstsrtenc.h
