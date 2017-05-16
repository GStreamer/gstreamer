plugin_LTLIBRARIES = libgstx265.la

libgstx265_la_SOURCES = gstx265enc.c
libgstx265_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(X265_CFLAGS)
libgstx265_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) \
	$(GST_LIBS) \
	$(X265_LIBS)
libgstx265_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstx265enc.h
