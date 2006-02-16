plugin_LTLIBRARIES = libgstasf.la

libgstasf_la_SOURCES = gstasfdemux.c gstasf.c asfheaders.c
libgstasf_la_CFLAGS = $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstasf_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS)\
		-lgstriff-@GST_MAJORMINOR@
libgstasf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstasfdemux.h asfheaders.h gstasfmux.h
