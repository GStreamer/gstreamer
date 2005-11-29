lib_LTLIBRARIES = libgstcheck-@GST_MAJORMINOR@.la

libgstcheck_@GST_MAJORMINOR@_la_DEPENDENCIES = \
	$(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
libgstcheck_@GST_MAJORMINOR@_la_SOURCES =	\
	gstcheck.c

libgstcheck_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS) $(CHECK_CFLAGS)
libgstcheck_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS) $(CHECK_LIBS) \
	$(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
libgstcheck_@GST_MAJORMINOR@_la_LDFLAGS = \
libgstbase_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

libgstcheck_@GST_MAJORMINOR@includedir =		\
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/check

libgstcheck_@GST_MAJORMINOR@include_HEADERS =	\
	gstcheck.h
