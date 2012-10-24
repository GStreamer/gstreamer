plugin_LTLIBRARIES = libgstzbar.la

libgstzbar_la_SOURCES = gstzbar.c

libgstzbar_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(ZBAR_CFLAGS)
libgstzbar_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ $(GST_BASE_LIBS) $(GST_LIBS) $(ZBAR_LIBS)
libgstzbar_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstzbar_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = gstzbar.h

