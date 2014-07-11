# variables used for enum generation
ORC_SOURCE=bad-video-orc
include $(top_srcdir)/common/orc.mak

lib_LTLIBRARIES = libgstbadvideo-@GST_API_VERSION@.la

CLEANFILES =

libgstbadvideo_@GST_API_VERSION@_la_SOURCES = \
	videoconvert.c gstvideoaggregator.c gstcms.c

nodist_libgstbadvideo_@GST_API_VERSION@_la_SOURCES = $(BUILT_SOURCES)

libgstbadvideo_@GST_API_VERSION@_la_CFLAGS = \
	-DGST_USE_UNSTABLE_API \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_CFLAGS) $(ORC_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS)

libgstbadvideo_@GST_API_VERSION@_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(ORC_LIBS) -lgstvideo-$(GST_API_VERSION)\
	$(top_builddir)/gst-libs/gst/base/libgstbadbase-$(GST_API_VERSION).la $(LIBM)
libgstbadvideo_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

noinst_HEADERS = gstcms.h videoconvert.h gstvideoaggregatorpad.h gstvideoaggregator.h
