plugin_LTLIBRARIES = libgstdtsdec.la

libgstdtsdec_la_SOURCES = gstdtsdec.c
libgstdtsdec_la_CFLAGS = -DGST_USE_UNSTABLE_API \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(ORC_CFLAGS)
libgstdtsdec_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) \
	-lgstaudio-@GST_API_VERSION@ \
	$(DTS_LIBS) $(ORC_LIBS)
libgstdtsdec_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdtsdec_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstdtsdec.h
