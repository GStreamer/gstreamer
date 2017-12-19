## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-wayland.la

libgstgl_wayland_la_SOURCES = \
	gstgldisplay_wayland.c \
	gstglwindow_wayland_egl.c \
	wayland_event_source.c

noinst_HEADERS = \
	gstgldisplay_wayland.h \
	gstglwindow_wayland_egl.h \
	wayland_event_source.h

libgstgl_waylandincludedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl/wayland
libgstgl_waylandinclude_HEADERS = \
	gstgldisplay_wayland.h

libgstgl_wayland_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_wayland_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
