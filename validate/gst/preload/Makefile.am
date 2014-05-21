libgstvalidate_preload_@GST_API_VERSION@_la_SOURCES = \
	gst-validate-monitor-preload.c

libgstvalidate_preload_@GST_API_VERSION@_la_CFLAGS = $(GST_ALL_CFLAGS)
libgstvalidate_preload_@GST_API_VERSION@_la_LDFLAGS =  $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS) $(top_builddir)/gst/validate/libgstvalidate-1.0.la
libgstvalidate_preload_@GST_API_VERSION@_la_LIBADD =  \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
	$(GST_ALL_LIBS)
libgstvalidate_preload_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/validate
libgstvalidate_preload_@GST_API_VERSION@include_HEADERS =

CLEANFILES =
