## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-viv-fb.la

libgstgl_viv_fb_la_SOURCES = \
	gstgldisplay_viv_fb.c \
	gstglwindow_viv_fb_egl.c 

noinst_HEADERS = \
	gstgldisplay_viv_fb.h \
	gstglwindow_viv_fb_egl.h

libgstgl_viv_fbincludedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl/viv-fb

libgstgl_viv_fb_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_viv_fb_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
