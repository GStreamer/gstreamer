# variables used for enum generation
ORC_SOURCE=bad-video-orc
include $(top_srcdir)/common/orc.mak

lib_LTLIBRARIES = libgstbadvideo-@GST_API_VERSION@.la

CLEANFILES =

libgstbadvideo_@GST_API_VERSION@_la_SOURCES = \
	videoconvert.c gstvideoaggregator.c gstcms.c

nodist_libgstbadvideo_@GST_API_VERSION@_la_SOURCES = $(BUILT_SOURCES)

libgstbadvideo_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/video
libgstbadvideo_@GST_API_VERSION@include_HEADERS = \
	gstvideoaggregator.h

nodist_libgstbadvideo_@GST_API_VERSION@include_HEADERS = $(built_headers)

libgstbadvideo_@GST_API_VERSION@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(ORC_CFLAGS) -I$(top_builddir)/gst-libs
libgstbadvideo_@GST_API_VERSION@_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS) $(ORC_LIBS) -lgstvideo-$(GST_API_VERSION)
libgstbadvideo_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

noinst_HEADERS = gstcms.h videoconvert.h
