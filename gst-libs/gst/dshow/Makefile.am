lib_LTLIBRARIES = libgstdshow-@GST_MAJORMINOR@.la

libgstdshow_@GST_MAJORMINOR@_la_SOURCES = gstdshow.cpp gstdshowfakesink.cpp gstdshowfakesrc.cpp gstdshowinterface.cpp
libgstdshow_@GST_MAJORMINOR@_la_CXXFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) -DLIBDSHOW_EXPORTS
libgstdshow_@GST_MAJORMINOR@_la_LIBADD = $(GST_BASE_LIBS)
libgstdshow_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_ALL_LDFLAGS)
libgstdshow_@GST_MAJORMINOR@_la_LIBTOOLFLAGS = --tag=disable-static

libgstdshow_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/dshow
libgstdshow_@GST_MAJORMINOR@include_HEADERS = gstdshow.h gstdshowfakesink.h gstdshowfakesrc.h gstdshowinterface.h
