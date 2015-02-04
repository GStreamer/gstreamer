plugin_LTLIBRARIES = libfaultinjection.la

libfaultinjection_la_SOURCES = \
	socket_interposer.c

noinst_HEADERS = \
	socket_interposer.h

libfaultinjection_la_CFLAGS = $(GST_ALL_CFLAGS)
libfaultinjection_la_LIBADD = $(GST_ALL_LIBS) $(top_builddir)/gst/validate/libgstvalidate-@GST_API_VERSION@.la
libfaultinjection_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_ALL_LDFLAGS)

CLEANFILES =
