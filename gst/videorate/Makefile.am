noinst_HEADERS = gstvideorate.h

plugin_LTLIBRARIES = libgstvideorate.la

libgstvideorate_la_SOURCES = gstvideorate.c
libgstvideorate_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstvideorate_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvideorate_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstvideorate_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)
