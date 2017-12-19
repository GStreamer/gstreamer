## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-dispmanx.la

libgstgl_dispmanx_la_SOURCES = \
	gstglwindow_dispmanx_egl.c

noinst_HEADERS = \
	gstglwindow_dispmanx_egl.h

libgstgl_dispmanx_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_dispmanx_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
