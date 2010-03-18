plugin_LTLIBRARIES = libgsticydemux.la

libgsticydemux_la_SOURCES = gsticydemux.c
libgsticydemux_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgsticydemux_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgsttag-@GST_MAJORMINOR@ $(GST_BASE_LIBS) $(GST_LIBS) $(ZLIB_LIBS)
libgsticydemux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsticydemux_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gsticydemux.h
