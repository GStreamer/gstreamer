
plugin_LTLIBRARIES = libgsta52dec.la

libgsta52dec_la_SOURCES = gsta52dec.c
libgsta52dec_la_CFLAGS = \
	$(GST_CFLAGS) \
	$(LIBOIL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) 
libgsta52dec_la_LIBADD = \
	$(A52DEC_LIBS) \
	$(LIBOIL_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_MAJORMINOR@
libgsta52dec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gsta52dec.h
