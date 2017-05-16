plugin_LTLIBRARIES = libgstopusparse.la

libgstopusparse_la_SOURCES = gstopus.c gstopusparse.c gstopusheader.c
libgstopusparse_la_CFLAGS = \
        -DGST_USE_UNSTABLE_API \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(OPUS_CFLAGS)
libgstopusparse_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	-lgsttag-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(OPUS_LIBS)
libgstopusparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBM)

noinst_HEADERS = gstopusparse.h gstopusheader.h
