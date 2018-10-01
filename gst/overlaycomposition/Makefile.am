noinst_HEADERS = gstoverlaycomposition.h

plugin_LTLIBRARIES = libgstoverlaycomposition.la

libgstoverlaycomposition_la_SOURCES = gstoverlaycomposition.c
libgstoverlaycomposition_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstoverlaycomposition_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstoverlaycomposition_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
