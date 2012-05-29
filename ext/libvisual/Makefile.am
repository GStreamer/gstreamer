plugin_LTLIBRARIES = libgstlibvisualgl.la

libgstlibvisualgl_la_SOURCES = visual-gl.c

libgstlibvisualgl_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(LIBVISUAL_CFLAGS) \
	-I$(top_srcdir)/gst-libs \
	-I$(top_srcdir)/gst-libs/gst/gl

libgstlibvisualgl_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la \
	$(GST_PLUGINS_BASE_LIBS) $(GST_LIBS) $(LIBVISUAL_LIBS)

libgstlibvisualgl_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstlibvisualgl_la_LIBTOOLFLAGS = --tag=disable-static

