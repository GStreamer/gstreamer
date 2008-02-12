lib_LTLIBRARIES = libgstdshow-@GST_MAJORMINOR@.la

libgstdshow_@GST_MAJORMINOR@_la_SOURCES = gstdshow.cpp gstdshowfakesink.cpp gstdshowfakesrc.cpp gstdshowinterface.cpp
libgstdshow_@GST_MAJORMINOR@_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
libgstdshow_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS)
libgstdshow_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_ALL_LDFLAGS)

libgstdshow_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/app
libgstdshow_@GST_MAJORMINOR@include_HEADERS = 