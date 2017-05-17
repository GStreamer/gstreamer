## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-cocoa.la

libgstgl_cocoa_la_SOURCES = \
	gstglwindow_cocoa.m \
	gstglcontext_cocoa.m \
	gstgldisplay_cocoa.m \
	gstglcaopengllayer.m

noinst_HEADERS = \
	gstglcontext_cocoa.h \
	gstgldisplay_cocoa.h \
	gstgl_cocoa_private.h

libgstgl_cocoaincludedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl/cocoa
libgstgl_cocoainclude_HEADERS = \
	gstglcaopengllayer.h \
	gstgldisplay_cocoa.h

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
	-fobjc-arc \
	$(GL_OBJCFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_OBJCFLAGS)

libgstgl_cocoa_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)

libgstgl_cocoa_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS) --tag=CC
