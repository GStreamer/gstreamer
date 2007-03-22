lib_LTLIBRARIES = libgstapp-@GST_MAJORMINOR@.la

libgstapp_@GST_MAJORMINOR@_la_SOURCES = gstappsrc.c gstappbuffer.c gstappsink.c
libgstapp_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstapp_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS)
libgstapp_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_ALL_LDFLAGS)

libgstapp_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/app
libgstapp_@GST_MAJORMINOR@include_HEADERS = \
	gstappsrc.h \
	gstappbuffer.h \
	gstappsink.h

