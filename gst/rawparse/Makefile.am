
plugin_LTLIBRARIES = libgstrawparse.la

libgstrawparse_la_SOURCES = gstrawparse.c gstaudioparse.c gstvideoparse.c plugin.c
libgstrawparse_la_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstrawparse_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@
libgstrawparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrawparse_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstaudioparse.h \
	gstrawparse.h \
	gstvideoparse.h
