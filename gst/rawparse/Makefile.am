
plugin_LTLIBRARIES = libgstrawparse.la

libgstrawparse_la_SOURCES = \
	gstrawparse.c \
	gstaudioparse.c \
	gstvideoparse.c \
	plugin.c
libgstrawparse_la_CFLAGS = \
		$(GST_PLUGINS_BASE_CFLAGS) \
		$(GST_BASE_CFLAGS) \
		$(GST_CFLAGS)
libgstrawparse_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-@GST_MAJORMINOR@ -lgstaudio-@GST_MAJORMINOR@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstrawparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrawparse_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstaudioparse.h \
	gstrawparse.h \
	gstvideoparse.h
