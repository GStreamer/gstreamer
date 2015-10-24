validateplugin_LTLIBRARIES = libgstvalidategapplication.la

libgstvalidategapplication_la_SOURCES = \
	gstvalidategapplication.c

libgstvalidategapplication_la_CFLAGS = $(GST_ALL_CFLAGS) $(GIO_CFLAGS)
libgstvalidategapplication_la_LIBADD = $(GST_ALL_LIBS) $(top_builddir)/gst/validate/libgstvalidate-@GST_API_VERSION@.la $(GIO_LIBS)
libgstvalidategapplication_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_ALL_LDFLAGS) $(GIO_LDFLAGS)

CLEANFILES =

