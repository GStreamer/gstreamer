plugin_LTLIBRARIES = libgstproxy.la

libgstproxy_la_SOURCES = \
	gstproxy.c \
	gstproxysink.c \
	gstproxysrc.c

noinst_HEADERS = \
	gstproxysink.h \
	gstproxysrc.h \
	gstproxy-priv.h

libgstproxy_la_CFLAGS = $(GST_CFLAGS)
libgstproxy_la_LIBADD = $(GST_LIBS)
libgstproxy_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstproxy_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
