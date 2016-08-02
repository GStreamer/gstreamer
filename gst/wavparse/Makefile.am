plugin_LTLIBRARIES = libgstwavparse.la

libgstwavparse_la_SOURCES = gstwavparse.c
libgstwavparse_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstwavparse_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstriff-@GST_API_VERSION@ \
	-lgstaudio-@GST_API_VERSION@ \
	-lgsttag-@GST_API_VERSION@ \
	-lgstpbutils-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(LIBM)
libgstwavparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwavparse_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstwavparse.h
