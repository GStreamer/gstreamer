plugin_LTLIBRARIES = libgstopusparse.la

libgstopusparse_la_SOURCES = gstopus.c gstopusparse.c gstopusheader.c
libgstopusparse_la_CFLAGS = \
        -DGST_USE_UNSTABLE_API \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(OPUS_CFLAGS) \
	-Dgst_opus_header_is_header=gst_opus_bad_header_is_header \
	-Dgst_opus_header_is_id_header=gst_opus_bad_header_is_id_header \
	-Dgst_opus_header_is_comment_header=gst_opus_bad_header_is_comment_header
libgstopusparse_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	-lgsttag-$(GST_API_VERSION) \
	-lgstpbutils-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(OPUS_LIBS)
libgstopusparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBM)

noinst_HEADERS = gstopusparse.h gstopusheader.h
