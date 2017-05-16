
plugin_LTLIBRARIES = libgstsmoothstreaming.la

libgstsmoothstreaming_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) \
    $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
    -DGST_USE_UNSTABLE_API $(LIBXML2_CFLAGS)
libgstsmoothstreaming_la_LIBADD = \
    $(top_builddir)/gst-libs/gst/codecparsers/libgstcodecparsers-$(GST_API_VERSION).la \
    $(top_builddir)/gst-libs/gst/adaptivedemux/libgstadaptivedemux-@GST_API_VERSION@.la \
    $(GST_PLUGINS_BASE_LIBS) \
    -lgsttag-$(GST_API_VERSION) \
    $(GST_BASE_LIBS) $(GST_LIBS) $(ZLIB_LIBS) $(LIBXML2_LIBS)
libgstsmoothstreaming_la_LDFLAGS = ${GST_PLUGIN_LDFLAGS}
libgstsmoothstreaming_la_SOURCES = gstsmoothstreaming-plugin.c \
	gstmssdemux.c \
	gstmssfragmentparser.c \
	gstmssmanifest.c

noinst_HEADERS = gstmssdemux.h \
	gstmssfragmentparser.h \
	gstmssmanifest.h
