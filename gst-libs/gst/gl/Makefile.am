
lib_LTLIBRARIES = libgstgl-@GST_API_VERSION@.la

EXTRA_DIST = \
	gstglwindow_x11.c \
	gstglwindow_x11_glx.c \
	gstglwindow_x11_egl.c \
	gstglwindow_win32.c \
	gstglwindow_win32_wgl.c \
	gstglwindow_win32_egl.c \
	gstglwindow_win32.h \
	gstglwindow_win32_wgl.h \
	gstglwindow_win32_egl.h \
	gstglwindow_x11.h \
	gstglwindow_x11_glx.h \
	gstglwindow_x11_wgl.h

libgstgl_@GST_API_VERSION@_la_SOURCES = \
	gstgldisplay.c \
	gstglmemory.c \
	gstglbufferpool.c \
	gstglfilter.c \
	gstglmixer.c \
        gstglshader.c \
        gstglshadervariables.c \
        gstgldownload.c \
        gstglupload.c \
        gstglwindow.c

#SUBDIRS = 
if HAVE_WINDOW_WIN32
libgstgl_@GST_API_VERSION@_la_SOURCES += gstglwindow_win32.c
if USE_WGL
libgstgl_@GST_API_VERSION@_la_SOURCES += gstglwindow_win32_wgl.c
endif
if USE_EGL
libgstgl_@GST_API_VERSION@_la_SOURCES += gstglwindow_win32_egl.c
endif
#SUBDIRS += win32
endif

if HAVE_WINDOW_COCOA
libgstgl_@GST_API_VERSION@_la_SOURCES += gstglwindow_cocoa.m
endif

if HAVE_WINDOW_X11
libgstgl_@GST_API_VERSION@_la_SOURCES += gstglwindow_x11.c
if USE_GLX
libgstgl_@GST_API_VERSION@_la_SOURCES += gstglwindow_x11_glx.c
endif
if USE_EGL
libgstgl_@GST_API_VERSION@_la_SOURCES += gstglwindow_x11_egl.c
endif
#SUBDIRS += x11
endif

libgstgl_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl
libgstgl_@GST_API_VERSION@include_HEADERS = \
	gstglwindow.h \
	gstgldisplay.h \
	gstglmemory.h \
	gstglbufferpool.h \
	gstgles2.h \
	gstglfilter.h \
	gstglmixer.h \
	gstglmixerpad.h \
    gstglshadervariables.h \
	gstglshader.h \
	gstgldownload.h \
	gstglupload.h

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

if HAVE_WINDOW_COCOA
libgstgl_@GST_API_VERSION@_la_LIBTOOLFLAGS = --tag=OBJC
else
libgstgl_@GST_API_VERSION@_la_LIBTOOLFLAGS = --tag=CC
endif
