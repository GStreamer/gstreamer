plugin_LTLIBRARIES = libgsta52dec.la

libgsta52dec_la_SOURCES = gsta52dec.c
libgsta52dec_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
        $(GST_BASE_CFLAGS) \
        $(GST_CFLAGS) \
	$(ORC_CFLAGS) \
	$(A52DEC_CFLAGS)
libgsta52dec_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
        $(GST_BASE_LIBS) \
        $(GST_LIBS) \
        -lgstaudio-$(GST_API_VERSION) \
	$(ORC_LIBS) \
	$(A52DEC_LIBS)
libgsta52dec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsta52dec_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gsta52dec.h
