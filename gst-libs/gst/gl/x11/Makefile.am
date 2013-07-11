## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-x11.la

libgstgl_x11_la_SOURCES = \
	gstglwindow_x11.c \
	x11_event_source.c

noinst_HEADERS = \
	gstglwindow_x11.h \
	x11_event_source.h

if USE_GLX
libgstgl_x11_la_SOURCES += gstglwindow_x11_glx.c
noinst_HEADERS += gstglwindow_x11_glx.h
endif
if USE_EGL
libgstgl_x11_la_SOURCES += gstglwindow_x11_egl.c
noinst_HEADERS += gstglwindow_x11_egl.h
endif

libgstgl_x11_la_CFLAGS = \
	$(GL_CFLAGS) \
	$(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-I$(top_srcdir)/gst-libs

libgstgl_x11_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
