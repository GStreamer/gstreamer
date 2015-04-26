
plugin_LTLIBRARIES = libgstsmooth.la

libgstsmooth_la_SOURCES = gstsmooth.c
libgstsmooth_la_CFLAGS = \
  $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)
libgstsmooth_la_LIBADD =  \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstsmooth_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstsmooth_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstsmooth.h
