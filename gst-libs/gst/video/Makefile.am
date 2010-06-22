noinst_LTLIBRARIES = libgstbasevideo-@GST_MAJORMINOR@.la

libgstbasevideo_source_c =			\
	gstbasevideocodec.c			\
	gstbasevideodecoder.c			\
	gstbasevideoutils.c			\
	$(NULL)

libgstbasevideo_source_h =			\
	gstbasevideocodec.h			\
	gstbasevideodecoder.h			\
	gstbasevideoutils.h			\
	$(NULL)

libgstbasevideo_@GST_MAJORMINOR@includedir =	\
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/video

libgstbasevideo_@GST_MAJORMINOR@_la_SOURCES =	\
	$(libgstbasevideo_source_c)

noinst_HEADERS =				\
	$(libgstbasevideo_source_h)

libgstbasevideo_@GST_MAJORMINOR@_la_CFLAGS =	\
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	$(GST_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(GST_PLUGINS_BASE_CFLAGS)		\
	$(NULL)

libgstbasevideo_@GST_MAJORMINOR@_la_LIBADD = 	\
	$(GST_LIBS)				\
	$(GST_BASE_LIBS)			\
	$(GST_PLUGINS_BASE_LIBS) 		\
	$(NULL)

libgstbasevideo_@GST_MAJORMINOR@_la_LDFLAGS =	\
	$(GST_ALL_LDFLAGS)			\
	$(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
