plugin_LTLIBRARIES = libgstwininet.la

libgstwininet_la_SOURCES = gstwininetsrc.c

libgstwininet_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstwininet_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) -lwininet
libgstwininet_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwininet_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstwininetsrc.h

