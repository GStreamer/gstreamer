plugin_LTLIBRARIES = libgstacmenc.la

libgstacmenc_la_SOURCES = acmenc.c
libgstacmenc_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(ACMENC_CFLAGS)
libgstacmenc_la_LIBADD =     \
    $(GST_BASE_LIBS)             \
    $(GST_PLUGINS_BASE_LIBS)     \
    -lgstriff-$(GST_API_VERSION)  \
    -lgstaudio-$(GST_API_VERSION) \
    -lgsttag-$(GST_API_VERSION)   \
    -lmsacm32

libgstacmenc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstacmenc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

