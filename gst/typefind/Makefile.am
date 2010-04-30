plugin_LTLIBRARIES = libgsttypefindfunctions.la

libgsttypefindfunctions_la_SOURCES = gsttypefindfunctions.c
libgsttypefindfunctions_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_CFLAGS) $(GIO_CFLAGS)
libgsttypefindfunctions_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttypefindfunctions_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-@GST_MAJORMINOR@.la \
	$(GST_LIBS) $(GIO_LIBS)

libgsttypefindfunctions_la_LIBTOOLFLAGS = --tag=disable-static
