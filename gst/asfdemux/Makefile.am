plugin_LTLIBRARIES = libgstasf.la

libgstasf_la_SOURCES = gstasfdemux.c gstasf.c asfheaders.c asfpacket.c gstrtspwms.c
libgstasf_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstasf_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS)\
		-lgstriff-@GST_MAJORMINOR@ -lgstrtsp-@GST_MAJORMINOR@ -lgstsdp-@GST_MAJORMINOR@
libgstasf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstasf_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstasfdemux.h asfheaders.h asfpacket.h gstasfmux.h gstrtspwms.h
