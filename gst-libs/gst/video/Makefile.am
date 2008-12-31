# variables used for enum/marshal generation
glib_enum_headers= video.h

glib_enum_define=GST_VIDEO
glib_enum_prefix=gst_video

built_sources = video-enumtypes.c
built_headers = video-enumtypes.h
BUILT_SOURCES = $(built_sources) $(built_headers)

lib_LTLIBRARIES = libgstvideo-@GST_MAJORMINOR@.la

CLEANFILES = $(BUILT_SOURCES)

libgstvideo_@GST_MAJORMINOR@_la_SOURCES = video.c gstvideosink.c gstvideofilter.c
nodist_libgstvideo_@GST_MAJORMINOR@_la_SOURCES = $(BUILT_SOURCES)

libgstvideoincludedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/video
libgstvideoinclude_HEADERS = video.h gstvideosink.h gstvideofilter.h
nodist_libgstvideoinclude_HEADERS = $(built_headers)

libgstvideo_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS)
libgstvideo_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstvideo_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

include $(top_srcdir)/common/glib-gen.mak
