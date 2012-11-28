## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-cocoa.la

libgstgl_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl
libgstglcocoaincludedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl/cocoa

libgstgl_cocoa_la_SOURCES = \
	gstglwindow_cocoa.m

libgstglcocoainclude_HEADERS = \
	gstglwindow_cocoa.h

libgstgl_cocoa_la_CFLAGS = \
	$(GL_CFLAGS) \
	$(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst-libs/gst/gl

libgstgl_cocoa_la_OBJCFLAGS = \
	$(GL_CFLAGS) \
	$(GL_OBJCFLAGS) \
	$(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst-libs/gst/gl

libgstgl_cocoa_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)
