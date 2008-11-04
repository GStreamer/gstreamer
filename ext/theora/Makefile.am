### FIXME: rename directory to theoraexp

plugin_LTLIBRARIES = libgsttheoraexp.la

noinst_HEADERS = theoradec.h

libgsttheoraexp_la_SOURCES = theoradec.c
libgsttheoraexp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(THEORADEC_CFLAGS)
libgsttheoraexp_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgsttag-$(GST_MAJORMINOR) \
	$(GST_LIBS) \
	$(THEORADEC_LIBS)
libgsttheoraexp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttheoraexp_la_LIBTOOLFLAGS = --tag=disable-static

