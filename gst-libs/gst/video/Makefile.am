
lib_LTLIBRARIES = libgstvideo-@GST_MAJORMINOR@.la

libgstvideo_@GST_MAJORMINOR@_la_SOURCES = video.c gstvideosink.c 

libgstvideoincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/video
libgstvideoinclude_HEADERS = video.h videosink.h

libgstvideo_@GST_MAJORMINOR@_la_LIBADD =
libgstvideo_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS)
libgstvideo_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIBS) $(GST_BASE_LIBS)
