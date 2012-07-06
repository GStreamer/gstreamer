
lib_LTLIBRARIES = libgstgl-@GST_API_VERSION@.la

EXTRA_DIST = \
	gstglwindow_x11.c \
	gstglwindow_x11ES2.c \
	gstglwindow_win32.c \
	gstglwindow_winCE.c \
	gstglwindow_cocoa.m

libgstgl_@GST_API_VERSION@_la_SOURCES = \
	gstgldisplay.c \
	gstglmemory.c \
	gstglmeta.c \
	gstglbufferpool.c \
	gstglfilter.c \
	gstglmixer.c \
        gstglshader.c \
        gstglshadervariables.c

if GL_BACKEND_WIN32
libgstgl_@GST_API_VERSION@_la_SOURCES += gstglwindow_win32.c
endif
if GL_BACKEND_COCOA
libgstgl_@GST_API_VERSION@_la_SOURCES += gstglwindow_cocoa.m
endif
if GL_BACKEND_X11
libgstgl_@GST_API_VERSION@_la_SOURCES += gstglwindow_x11.c
endif
if GL_BACKEND_X11ES2
libgstgl_@GST_API_VERSION@_la_SOURCES += gstglwindow_x11ES2.c
endif

libgstgl_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl
libgstgl_@GST_API_VERSION@include_HEADERS = \
	gstglwindow.h \
	gstgldisplay.h \
	gstglmemory.h \
	gstglmeta.h \
	gstglbufferpool.h \
	gstgles2.h \
	gstglfilter.h \
	gstglmixer.h \
	gstglmixerpad.h \
        gstglshadervariables.h \
	gstglshader.h

libgstgl_@GST_API_VERSION@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) \
	$(GL_LIBS)

libgstgl_@GST_API_VERSION@_la_CFLAGS = \
	$(GL_CFLAGS) $(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstgl_@GST_API_VERSION@_la_OBJCFLAGS = \
	$(GL_CFLAGS) $(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)

libgstgl_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

if GL_BACKEND_COCOA
libgstgl_@GST_API_VERSION@_la_LIBTOOLFLAGS = --tag=OBJC
else
libgstgl_@GST_API_VERSION@_la_LIBTOOLFLAGS = --tag=CC
endif
