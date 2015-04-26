
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
	-lgstvideo-@GST_API_VERSION@ -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstrawparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstrawparse_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstaudioparse.h \
	gstrawparse.h \
	gstvideoparse.h
