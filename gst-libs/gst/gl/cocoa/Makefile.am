## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-cocoa.la

libgstgl_cocoa_la_SOURCES = \
	gstglwindow_cocoa.m

noinst_HEADERS = \
	gstglwindow_cocoa.h

libgstgl_cocoa_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(GL_CFLAGS) \
	$(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_cocoa_la_OBJCFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(GL_CFLAGS) \
	$(GL_OBJCFLAGS) \
	$(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_cocoa_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
