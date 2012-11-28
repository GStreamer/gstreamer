## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-wayland.la

libgstgl_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl
libgstglwaylandincludedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl/wayland

libgstgl_wayland_la_SOURCES = \
	gstglwindow_wayland_egl.c \
	wayland_event_source.c

libgstglwaylandinclude_HEADERS = \
	gstglwindow_wayland_egl.h \
	wayland_event_source.h

libgstgl_wayland_la_CFLAGS = \
	$(GL_CFLAGS) \
	$(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst-libs/gst/gl

libgstgl_wayland_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)
