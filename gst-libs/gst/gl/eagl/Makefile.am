## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-eagl.la

libgstgl_eagl_la_SOURCES = \
	gstglwindow_eagl.m \
	gstglcontext_eagl.m

noinst_HEADERS = \
	gstglwindow_eagl.h \
	gstglcontext_eagl.h

libgstgl_eagl_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_eagl_la_OBJCFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	-fobjc-arc \
	$(GL_CFLAGS) \
	$(GL_OBJCFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_eagl_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)

libgstgl_eagl_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS) --tag=CC
