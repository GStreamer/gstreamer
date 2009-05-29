plugin_LTLIBRARIES = libgstshapewipe.la

libgstshapewipe_la_SOURCES = gstshapewipe.c

libgstshapewipe_la_CFLAGS = $(GIO_CFLAGS) $(GST_CFLAGS) $(GST_CONTROLLER_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
libgstshapewipe_la_LIBADD = $(GIO_LIBS) $(GST_LIBS) $(GST_CONTROLLER_LIBS) $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_MAJORMINOR@
libgstshapewipe_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstshapewipe_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstshapewipe.h

