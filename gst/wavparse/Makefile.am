plugin_LTLIBRARIES = libgstwavparse.la

libgstwavparse_la_SOURCES = gstwavparse.c
libgstwavparse_la_CFLAGS = \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst-libs
libgstwavparse_la_LIBADD = \
	-lgstriff-@GST_MAJORMINOR@ \
	-lgstaudio-@GST_MAJORMINOR@ \
	-lgsttag-@GST_MAJORMINOR@ \
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) \
	$(LIBM)
libgstwavparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwavparse_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstwavparse.h
