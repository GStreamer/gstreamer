
lib_LTLIBRARIES = libgstgl-@GST_API_VERSION@.la

SUBDIRS =

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

libgstgl_@GST_API_VERSION@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_API_VERSION) \
	$(GST_BASE_LIBS) $(GST_LIBS) \
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

libgstgl_@GST_API_VERSION@_la_CFLAGS = \
	$(GL_CFLAGS) \
	$(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS)
