
plugin_LTLIBRARIES = libgstlegacyrawparse.la

libgstlegacyrawparse_la_SOURCES = \
	gstaudioparse.c \
	gstvideoparse.c \
	plugin.c
libgstlegacyrawparse_la_CFLAGS = \
		$(GST_PLUGINS_BASE_CFLAGS) \
		$(GST_BASE_CFLAGS) \
		$(GST_CFLAGS)
libgstlegacyrawparse_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-@GST_API_VERSION@ -lgstaudio-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstlegacyrawparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstaudioparse.h \
	gstvideoparse.h
