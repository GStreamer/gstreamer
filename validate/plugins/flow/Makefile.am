validateplugin_LTLIBRARIES = libgstvalidateflow.la

libgstvalidateflow_la_SOURCES = \
	gstvalidateflow.c formatting.c
EXTRA_DIST = \
	formatting.h

libgstvalidateflow_la_CFLAGS = $(GST_ALL_CFLAGS) $(GST_PBUTILS_CFLAGS) $(GIO_CFLAGS)
libgstvalidateflow_la_LIBADD = $(GST_ALL_LIBS) $(top_builddir)/gst/validate/libgstvalidate-@GST_API_VERSION@.la  $(GST_PBUTILS_LIBS) $(GIO_LIBS)
libgstvalidateflow_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_ALL_LDFLAGS) $(GIO_LDFLAGS)

CLEANFILES =

