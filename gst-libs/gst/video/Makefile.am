librarydir = $(libdir)/gstreamer-@GST_MAJORMINOR@

library_LTLIBRARIES = libgstvideo.la

libgstvideo_la_SOURCES = video.c gstvideosink.c 

libgstvideoincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/video
libgstvideoinclude_HEADERS = video.h videosink.h

libgstvideo_la_LIBADD =
libgstvideo_la_CFLAGS = $(GST_CFLAGS)
libgstvideo_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
