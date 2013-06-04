## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-x11.la

libgstgl_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl
libgstglx11includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl/x11

libgstgl_x11_la_SOURCES = \
	gstglwindow_x11.c

libgstglx11include_HEADERS = \
	gstglwindow_x11.h

if USE_GLX
libgstgl_x11_la_SOURCES += gstglwindow_x11_glx.c
libgstglx11include_HEADERS += gstglwindow_x11_glx.h
endif
if USE_EGL
libgstgl_x11_la_SOURCES += gstglwindow_x11_egl.c
libgstglx11include_HEADERS += gstglwindow_x11_egl.h
endif

libgstgl_x11_la_CFLAGS = \
	$(GL_CFLAGS) \
	$(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst-libs/gst/gl

libgstgl_x11_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
