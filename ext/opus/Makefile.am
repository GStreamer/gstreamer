plugin_LTLIBRARIES = libgstopus.la

libgstopus_la_SOURCES = gstopus.c gstopusdec.c gstopusenc.c gstopusheader.c gstopuscommon.c
libgstopus_la_CFLAGS = \
        -DGST_USE_UNSTABLE_API \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(OPUS_CFLAGS)
libgstopus_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(OPUS_LIBS)
libgstopus_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(LIBM)
libgstopus_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstopusenc.h gstopusdec.h gstopusheader.h gstopuscommon.h
