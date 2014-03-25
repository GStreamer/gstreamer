## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-android.la

libgstgl_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl
libgstglandroidincludedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl/android

libgstgl_android_la_SOURCES = \
	gstglwindow_android_egl.c

libgstglandroidinclude_HEADERS = \
	gstglwindow_android_egl.h

libgstgl_android_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_android_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
