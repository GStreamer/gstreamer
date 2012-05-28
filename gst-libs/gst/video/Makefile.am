
lib_LTLIBRARIES = libgstbasevideo-@GST_API_VERSION@.la

CLEANFILES = $(BUILT_SOURCES)

libgstbasevideo_@GST_API_VERSION@_la_SOURCES = \
	gstsurfacemeta.c \
	gstsurfaceconverter.c \
	videocontext.c

libgstbasevideo_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/video
libgstbasevideo_@GST_API_VERSION@include_HEADERS = \
	gstsurfacemeta.h \
	gstsurfaceconverter.h \
	videocontext.h

libgstbasevideo_@GST_API_VERSION@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS)
libgstbasevideo_@GST_API_VERSION@_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) -lgstvideo-@GST_API_VERSION@
libgstbasevideo_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

