
lib_LTLIBRARIES = libgstgl-@GST_MAJORMINOR@.la

libgstgl_@GST_MAJORMINOR@_la_SOURCES = \
	gstgldisplay.c \
	gstglbuffer.c

libgstgl_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/gl
libgstgl_@GST_MAJORMINOR@include_HEADERS = \
	gstgldisplay.h \
	gstglbuffer.h

libgstgl_@GST_MAJORMINOR@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_BASE_LIBS) $(GST_LIBS) \
	$(X_LIBS) $(GL_LIBS)

libgstgl_@GST_MAJORMINOR@_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS)

libgstgl_@GST_MAJORMINOR@_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

