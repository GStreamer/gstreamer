# variables used for enum generation
lib_LTLIBRARIES = libgstbadvideo-@GST_API_VERSION@.la

CLEANFILES =

libgstbadvideo_@GST_API_VERSION@_la_SOURCES = \
	gstvideoaggregator.c

nodist_libgstbadvideo_@GST_API_VERSION@_la_SOURCES = $(BUILT_SOURCES)

libgstbadvideo_@GST_API_VERSION@_la_CFLAGS = \
	-DGST_USE_UNSTABLE_API \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_CFLAGS) $(ORC_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS)

libgstbadvideo_@GST_API_VERSION@_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/base/libgstbadbase-$(GST_API_VERSION).la \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) $(ORC_LIBS) $(LIBM)

libgstbadvideo_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

libgstvideo_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/video
libgstvideo_@GST_API_VERSION@include_HEADERS = gstvideoaggregatorpad.h gstvideoaggregator.h
