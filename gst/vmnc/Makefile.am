plugin_LTLIBRARIES = libgstvmnc.la

noinst_HEADERS = vmncdec.h

libgstvmnc_la_SOURCES = vmncdec.c
libgstvmnc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstvmnc_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS)
libgstvmnc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvmnc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
