lib_LTLIBRARIES = libgsturidownloader-@GST_API_VERSION@.la

libgsturidownloader_@GST_API_VERSION@_la_SOURCES = \
	gstfragment.c gsturidownloader.c

libgsturidownloader_@GST_API_VERSION@includedir = \
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/uridownloader

libgsturidownloader_@GST_API_VERSION@include_HEADERS = \
	gstfragment.h gsturidownloader.h gsturidownloader_debug.h

libgsturidownloader_@GST_API_VERSION@_la_CFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	$(GST_CFLAGS)

libgsturidownloader_@GST_API_VERSION@_la_LIBADD = \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)

libgsturidownloader_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)
