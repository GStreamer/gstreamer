## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-win32.la

libgstgl_win32_la_SOURCES = \
	gstglwindow_win32.c \
	win32_message_source.c

noinst_HEADERS = \
	gstglwindow_win32.h \
	win32_message_source.h

if USE_WGL
libgstgl_win32_la_SOURCES += gstglcontext_wgl.c
noinst_HEADERS += gstglcontext_wgl.h
endif

libgstgl_win32_la_CFLAGS = \
	-DGST_EXPORTS \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_win32_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)
