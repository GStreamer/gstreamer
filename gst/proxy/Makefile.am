plugin_LTLIBRARIES = libgstproxy.la

libgstproxy_la_SOURCES = gstproxy.c gstproxysink.c gstproxysrc.c

libgstproxy_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstproxy_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS)
libgstproxy_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstproxysink.h gstproxysink-priv.h gstproxysrc.h gstproxysrc-priv.h
