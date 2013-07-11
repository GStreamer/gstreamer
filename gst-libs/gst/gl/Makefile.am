
lib_LTLIBRARIES = libgstgl-@GST_API_VERSION@.la

SUBDIRS = glprototypes
DIST_SUBDIRS = glprototypes x11 win32 cocoa wayland

noinst_HEADERS =

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
        gstglwindow.c \
        gstglapi.c \
        gstglfeature.c \
        gstglutils.c \
        gstglframebuffer.c

libgstgl_@GST_API_VERSION@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(GL_LIBS)

if HAVE_WINDOW_WIN32
SUBDIRS += win32
libgstgl_@GST_API_VERSION@_la_LIBADD += win32/libgstgl-win32.la
endif

if HAVE_WINDOW_COCOA
SUBDIRS += cocoa
libgstgl_@GST_API_VERSION@_la_LIBADD += cocoa/libgstgl-cocoa.la
endif

if HAVE_WINDOW_X11
SUBDIRS += x11
libgstgl_@GST_API_VERSION@_la_LIBADD += x11/libgstgl-x11.la
endif

if HAVE_WINDOW_WAYLAND
SUBDIRS += wayland
libgstgl_@GST_API_VERSION@_la_LIBADD += wayland/libgstgl-wayland.la
endif

if USE_EGL

if HAVE_WINDOW_WAYLAND
libgstgl_@GST_API_VERSION@_la_SOURCES += gstglegl.c
noinst_HEADERS += gstglegl.h
endif

if !HAVE_WINDOW_WAYLAND
if HAVE_WINDOW_X11
libgstgl_@GST_API_VERSION@_la_SOURCES += gstglegl.c
noinst_HEADERS += gstglegl.h
endif
endif

endif

libgstgl_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl
libgstgl_@GST_API_VERSION@include_HEADERS = \
	gstglconfig.h \
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
	gstglupload.h \
	gstglapi.h \
	gstglfeature.h \
	gstglutils.h \
	gstglframebuffer.h \
	gstgl_fwd.h \
	gl.h

libgstgl_@GST_API_VERSION@_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	$(GL_CFLAGS) \
	$(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(GL_CFLAGS)

libgstgl_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)
