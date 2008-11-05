
lib_LTLIBRARIES = libgstgl-@GST_MAJORMINOR@.la

libgstgl_@GST_MAJORMINOR@_la_SOURCES = \
  gstglwindow_x11.c \
	gstgldisplay.c \
	gstglbuffer.c \
	gstglfilter.c \
	gstglshader.c

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
	${top_builddir}/gst-libs/gst/freeglut/libgstfreeglut-$(GST_MAJORMINOR).la \
	-lGLU -lGLEW $(GL_LIBS) $(X_LIBS)

libgstgl_@GST_MAJORMINOR@_la_CFLAGS = \
	$(GL_CFLAGS) $(X_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) \
	-I$(top_srcdir)/gst-libs/gst/freeglut

libgstgl_@GST_MAJORMINOR@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

