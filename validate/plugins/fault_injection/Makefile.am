validateplugin_LTLIBRARIES = libgstvalidatefaultinjection.la

libgstvalidatefaultinjection_la_SOURCES = \
	socket_interposer.c

libgstvalidatefaultinjection_la_CFLAGS = $(GST_ALL_CFLAGS)
libgstvalidatefaultinjection_la_LIBADD = $(GST_ALL_LIBS) $(top_builddir)/gst/validate/libgstvalidate-@GST_API_VERSION@.la
libgstvalidatefaultinjection_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(GST_ALL_LDFLAGS)

CLEANFILES =
