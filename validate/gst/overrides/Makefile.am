lib_LTLIBRARIES = libgstvalidate-default-overrides-@GST_API_VERSION@.la
libgstvalidate_default_overrides_@GST_API_VERSION@_la_SOURCES = \
    gst-validate-default-overrides.c

libgstvalidate_default_overrides_@GST_API_VERSION@_la_CFLAGS = $(GST_ALL_CFLAGS) $(GIO_CFLAGS)
libgstvalidate_default_overrides_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS) $(GIO_LDFLAGS) $(top_builddir)/gst/validate/libgstvalidate-1.0.la
libgstvalidate_default_overrides_@GST_API_VERSION@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
	$(GST_ALL_LIBS) $(GIO_LIBS)
libgstvalidate_default_overrides_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/validate
libgstvalidate_default_overrides_@GST_API_VERSION@include_HEADERS =

CLEANFILES =
