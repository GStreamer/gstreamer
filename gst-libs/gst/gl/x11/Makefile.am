## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-x11.la

libgstgl_x11_la_SOURCES = \
	gstgldisplay_x11.c \
	gstglwindow_x11.c \
	x11_event_source.c

noinst_HEADERS = \
	gstglwindow_x11.h \
	x11_event_source.h

libgstgl_x11includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl/x11
libgstgl_x11include_HEADERS = \
	gstgldisplay_x11.h

if USE_GLX
libgstgl_x11_la_SOURCES += gstglcontext_glx.c
noinst_HEADERS += gstglcontext_glx.h
endif

libgstgl_x11_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_x11_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
