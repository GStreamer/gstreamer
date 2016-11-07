## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-android.la

libgstgl_android_la_SOURCES = \
	gstglwindow_android_egl.c

noinst_HEADERS = \
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
