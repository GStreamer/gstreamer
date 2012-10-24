plugin_LTLIBRARIES = libgstximagesink.la

libgstximagesink_la_SOURCES =  ximagesink.c ximage.c ximagepool.c
libgstximagesink_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(X_CFLAGS)
libgstximagesink_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) \
	$(X_LIBS) $(XSHM_LIBS)
libgstximagesink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstximagesink_la_DEPENDENCIES = $(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_API_VERSION).la
libgstximagesink_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = ximagesink.h ximagepool.h
