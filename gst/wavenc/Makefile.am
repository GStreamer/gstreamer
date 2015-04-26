plugin_LTLIBRARIES = libgstwavenc.la

libgstwavenc_la_SOURCES = gstwavenc.c
libgstwavenc_la_CFLAGS =  \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstwavenc_la_LIBADD = 	\
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_API_VERSION@ \
	-lgstriff-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstwavenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwavenc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstwavenc.h
