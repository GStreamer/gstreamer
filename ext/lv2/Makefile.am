plugin_LTLIBRARIES = libgstlv2.la

libgstlv2_la_SOURCES = gstlv2.c gstlv2utils.c gstlv2filter.c gstlv2source.c
libgstlv2_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(GST_AUDIO_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) $(LILV_CFLAGS)
libgstlv2_la_LIBADD = \
	$(GST_AUDIO_LIBS) \
	$(GST_BASE_LIBS) \
	$(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_API_VERSION) \
	$(LIBM) $(LILV_LIBS)
libgstlv2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstlv2.h gstlv2utils.h
