lib_LTLIBRARIES = libgstisoff-@GST_API_VERSION@.la

libgstisoff_@GST_API_VERSION@_la_SOURCES = \
	gstisoff.c

libgstisoff_@GST_API_VERSION@includedir = \
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/isoff

libgstisoff_@GST_API_VERSION@include_HEADERS = \
	gstisoff.h

libgstisoff_@GST_API_VERSION@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS)

libgstisoff_@GST_API_VERSION@_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)

libgstisoff_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)
