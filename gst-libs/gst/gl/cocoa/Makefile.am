## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-cocoa.la

libgstgl_cocoa_la_SOURCES = \
	gstglwindow_cocoa.m \
	gstglcontext_cocoa.m

noinst_HEADERS = \
	gstglwindow_cocoa.h \
	gstglcontext_cocoa.h \
	gstgl_cocoa_private.h

libgstgl_cocoa_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_cocoa_la_OBJCFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GL_OBJCFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_OBJCFLAGS)

libgstgl_cocoa_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)

libgstgl_cocoa_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS) --tag=CC
