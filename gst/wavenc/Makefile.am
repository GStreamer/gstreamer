plugin_LTLIBRARIES = libgstwavenc.la

libgstwavenc_la_SOURCES = gstwavenc.c
libgstwavenc_la_CFLAGS =  \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstwavenc_la_LIBADD = 	\
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstriff-@GST_MAJORMINOR@ \
	$(GST_LIBS)
libgstwavenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwavenc_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstwavenc.h
