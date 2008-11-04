plugin_LTLIBRARIES = libgstapp.la

libgstapp_la_SOURCES = gstapp.c
libgstapp_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstapp_la_LIBADD = $(GST_BASE_LIBS) $(top_builddir)/gst-libs/gst/app/libgstapp-@GST_MAJORMINOR@.la
libgstapp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstapp_la_LIBTOOLFLAGS = --tag=disable-static

