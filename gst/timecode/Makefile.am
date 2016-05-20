plugin_LTLIBRARIES = libgsttimecode.la

libgsttimecode_la_SOURCES = plugin.c gsttimecodestamper.c

libgsttimecode_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgsttimecode_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-1.0 $(GST_LIBS)
libgsttimecode_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttimecode_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gsttimecodestamper.h
