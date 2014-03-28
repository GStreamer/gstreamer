## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-egl.la

libgstgl_egl_la_SOURCES = \
	gstgldisplay_egl.c \
	gstglcontext_egl.c \
	gsteglimagememory.c

noinst_HEADERS = \
	gstglcontext_egl.h

libgstgl_eglincludedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl/egl
libgstgl_eglinclude_HEADERS = \
	gstgldisplay_egl.h \
	gsteglimagememory.h

libgstgl_egl_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_egl_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
