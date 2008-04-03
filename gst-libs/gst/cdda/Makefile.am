lib_LTLIBRARIES = libgstcdda-@GST_MAJORMINOR@.la

libgstcdda_@GST_MAJORMINOR@_la_SOURCES = \
	gstcddabasesrc.c \
	sha1.c \
	sha1.h

libgstcdda_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/cdda
libgstcdda_@GST_MAJORMINOR@include_HEADERS = \
	gstcddabasesrc.h

libgstcdda_@GST_MAJORMINOR@_la_LIBADD = $(GST_LIBS) $(GST_BASE_LIBS) \
  $(top_builddir)/gst-libs/gst/tag/libgsttag-@GST_MAJORMINOR@.la
libgstcdda_@GST_MAJORMINOR@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstcdda_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)
