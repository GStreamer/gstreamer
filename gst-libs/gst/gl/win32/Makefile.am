## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-win32.la

libgstgl_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl
libgstglwin32includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl/win32

libgstgl_win32_la_SOURCES = \
	gstglwindow_win32.c

libgstglwin32include_HEADERS = \
	gstglwindow_win32.h

if USE_WGL
libgstgl_win32_la_SOURCES += gstglwindow_win32_wgl.c
libgstglwin32include_HEADERS += gstglwindow_win32_wgl.h
endif
if USE_EGL
libgstgl_win32_la_SOURCES += gstglwindow_win32_egl.c
libgstglwin32include_HEADERS += gstglwindow_win32_egl.h
endif

libgstgl_win32_la_CFLAGS = \
	$(GL_CFLAGS) \
	$(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst-libs/gst/gl

libgstgl_win32_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)
