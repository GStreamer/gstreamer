plugin_LTLIBRARIES = libgstlibvisual.la

libgstlibvisual_la_SOURCES = plugin.c visual.c
libgstlibvisual_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(LIBVISUAL_CFLAGS)
libgstlibvisual_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/audio/libgstaudio-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/video/libgstvideo-$(GST_API_VERSION).la \
	$(top_builddir)/gst-libs/gst/pbutils/libgstpbutils-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) $(GST_LIBS) $(LIBVISUAL_LIBS)
libgstlibvisual_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstlibvisual_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = visual.h
