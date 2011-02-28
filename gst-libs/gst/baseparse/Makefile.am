
lib_LTLIBRARIES = libgstbaseparse-@GST_MAJORMINOR@.la

CLEANFILES = $(BUILT_SOURCES)

libgstbaseparse_@GST_MAJORMINOR@_la_SOURCES = \
	gstbaseparse.c

libgstbaseparse_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/baseparse
libgstbaseparse_@GST_MAJORMINOR@include_HEADERS = \
	gstbaseparse.h

libgstbaseparse_@GST_MAJORMINOR@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS)
libgstbaseparse_@GST_MAJORMINOR@_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) $(GST_LIBS)
libgstbaseparse_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

