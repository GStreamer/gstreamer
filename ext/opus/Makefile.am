plugin_LTLIBRARIES = libgstopus.la

libgstopus_la_SOURCES = gstopus.c gstopusdec.c gstopusenc.c gstopusparse.c gstopusheader.c gstopuscommon.c gstrtpopuspay.c gstrtpopusdepay.c
libgstopus_la_CFLAGS = \
        -DGST_USE_UNSTABLE_API \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(OPUS_CFLAGS)
libgstopus_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	-lgsttag-$(GST_API_VERSION) -lgstrtp-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(OPUS_LIBS)
libgstopus_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBM)
libgstopus_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstopusenc.h gstopusdec.h gstopusparse.h gstopusheader.h gstopuscommon.h gstrtpopuspay.h gstrtpopusdepay.h
