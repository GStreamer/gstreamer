
lib_LTLIBRARIES = libgstgl-@GST_MAJORMINOR@.la

EXTRA_DIST = \
	gstglwindow_x11.c \
	gstglwindow_win32.c \
	gstglwindow_cocoa.m

libgstgl_@GST_MAJORMINOR@_la_SOURCES = \
	gstgldisplay.c \
	gstglbuffer.c \
	gstglfilter.c \
	gstglshader.c

if GL_BACKEND_WIN32
libgstgl_@GST_MAJORMINOR@_la_SOURCES += gstglwindow_win32.c
endif
if GL_BACKEND_COCOA
libgstgl_@GST_MAJORMINOR@_la_SOURCES += gstglwindow_cocoa.m
endif
if GL_BACKEND_X11
libgstgl_@GST_MAJORMINOR@_la_SOURCES += gstglwindow_x11.c
endif

libgstgl_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/gl
libgstgl_@GST_MAJORMINOR@include_HEADERS = \
	gstglwindow.h \
	gstgldisplay.h \
	gstglbuffer.h \
	gstglfilter.h \
	gstglshader.h

libgstgl_@GST_MAJORMINOR@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS) \
	$(GL_LIBS)

libgstgl_@GST_MAJORMINOR@_la_CFLAGS = \
	$(GL_CFLAGS) $(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstgl_@GST_MAJORMINOR@_la_OBJCFLAGS = \
	$(GL_CFLAGS) $(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)

libgstgl_@GST_MAJORMINOR@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

