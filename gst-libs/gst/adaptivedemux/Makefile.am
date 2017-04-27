
lib_LTLIBRARIES = libgstadaptivedemux-@GST_API_VERSION@.la

CLEANFILES = $(BUILT_SOURCES)

libgstadaptivedemux_@GST_API_VERSION@_la_SOURCES = \
	gstadaptivedemux.c

libgstadaptivedemux_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/adaptivedemux

noinst_HEADERS = gstadaptivedemux.h

libgstadaptivedemux_@GST_API_VERSION@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS)
libgstadaptivedemux_@GST_API_VERSION@_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/uridownloader/libgsturidownloader-$(GST_API_VERSION).la \
	$(GST_PLUGINS_BASE_LIBS) -lgstapp-$(GST_API_VERSION) $(GST_BASE_LIBS) $(GST_LIBS)

libgstadaptivedemux_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)
