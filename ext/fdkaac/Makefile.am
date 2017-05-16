plugin_LTLIBRARIES = libgstfdkaac.la

libgstfdkaac_la_SOURCES = plugin.c gstfdkaacenc.c gstfdkaacdec.c
libgstfdkaac_la_CFLAGS = -DGST_USE_UNSTABLE_API \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) $(FDK_AAC_CFLAGS)
libgstfdkaac_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_API_VERSION@ -lgstpbutils-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(FDK_AAC_LIBS)
libgstfdkaac_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstfdkaacenc.h gstfdkaacdec.h
