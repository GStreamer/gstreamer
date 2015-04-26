
plugin_LTLIBRARIES = libgsty4menc.la

libgsty4menc_la_SOURCES = gsty4mencode.c
libgsty4menc_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgsty4menc_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) $(GST_LIBS)
libgsty4menc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsty4menc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gsty4mencode.h
