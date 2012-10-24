plugin_LTLIBRARIES = libgstflac.la

libgstflac_la_SOURCES = gstflac.c gstflacdec.c gstflacenc.c gstflactag.c
libgstflac_la_CFLAGS = -DGST_USE_UNSTABLE_API \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(FLAC_CFLAGS)
libgstflac_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgsttag-$(GST_API_VERSION) \
	-lgstaudio-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(FLAC_LIBS)
libgstflac_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstflac_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstflacenc.h gstflacdec.h gstflactag.h
