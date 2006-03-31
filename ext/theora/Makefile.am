plugin_LTLIBRARIES = libgsttheoraexp.la

noinst_HEADERS = theoradec.h

libgsttheoraexp_la_SOURCES = theoradec.c
libgsttheoraexp_la_CFLAGS = $(GST_CFLAGS) $(THEORADEC_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgsttheoraexp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttheoraexp_la_LIBADD = \
	-lgsttag-@GST_MAJORMINOR@ \
	$(GST_LIBS) \
	$(THEORADEC_LIBS) \
	$(GST_PLUGINS_BASE_LIBS)
