
lib_LTLIBRARIES = libgstbasevideo-@GST_MAJORMINOR@.la

CLEANFILES = $(BUILT_SOURCES)

libgstbasevideo_@GST_MAJORMINOR@_la_SOURCES = \
	gstbasevideoutils.c \
	gstbasevideocodec.c \
	gstbasevideodecoder.c \
	gstbasevideoencoder.c

libgstbasevideo_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/video
libgstbasevideo_@GST_MAJORMINOR@include_HEADERS = \
	gstbasevideocodec.h \
	gstbasevideodecoder.h \
	gstbasevideoencoder.h

libgstbasevideo_@GST_MAJORMINOR@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS)
libgstbasevideo_@GST_MAJORMINOR@_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS) -lgstvideo-@GST_MAJORMINOR@
libgstbasevideo_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

