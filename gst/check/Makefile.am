lib_LTLIBRARIES = libgstcheck-@GST_MAJORMINOR@.la

libgstcheck_@GST_MAJORMINOR@_la_DEPENDENCIES = \
	../libgstreamer-@GST_MAJORMINOR@.la
libgstcheck_@GST_MAJORMINOR@_la_SOURCES =	\
	gstcheck.c

libgstcheck_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS) $(CHECK_CFLAGS)
libgstcheck_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS) $(CHECK_LIBS) \
	../libgstreamer-@GST_MAJORMINOR@.la
libgstcheck_@GST_MAJORMINOR@_la_LDFLAGS = \
	-no-undefined

libgstcheck_@GST_MAJORMINOR@includedir =		\
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/check

libgstcheck_@GST_MAJORMINOR@include_HEADERS =	\
	gstcheck.h
