plugin_LTLIBRARIES = libgstpbtypes.la

libgstpbtypes_la_SOURCES = gstpbtypes.c
libgstpbtypes_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstpbtypes_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstpbtypes_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS) $(GST_LIBS)

libgstpbtypes_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
