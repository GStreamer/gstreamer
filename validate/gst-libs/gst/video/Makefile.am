libgstvalidatevideo_@GST_API_VERSION@_la_SOURCES = gstvalidatessim.c gssim.c
libgstvalidatevideo_@GST_API_VERSION@include_HEADERS = gstvalidatessim.h gssim.h

libgstvalidatevideo_@GST_API_VERSION@_la_CFLAGS = $(GST_ALL_CFLAGS) $(GST_VIDEO_CFLAGS) $(GIO_CFLAGS) $(CAIRO_CFLAGS) -I$(top_builddir)
libgstvalidatevideo_@GST_API_VERSION@_la_LIBADD = $(GST_ALL_LIBS) $(GST_VIDEO_LIBS) $(GIO_LIBS) $(CAIRO_LIBS) $(top_builddir)/gst/validate/libgstvalidate-@GST_API_VERSION@.la
libgstvalidatevideo_@GST_API_VERSION@_la_LDFLAGS = $(GST_ALL_LDFLAGS)

lib_LTLIBRARIES = libgstvalidatevideo-@GST_API_VERSION@.la

libgstvalidatevideo_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/lib/validate/video

CLEANFILES =
