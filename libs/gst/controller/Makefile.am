lib_LTLIBRARIES = libgstcontroller-@GST_MAJORMINOR@.la

libgstcontroller_@GST_MAJORMINOR@_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/controller
libgstcontroller_@GST_MAJORMINOR@_include_HEADERS = \
	gst-controller.h

libgstcontroller_@GST_MAJORMINOR@_la_SOURCES = \
	lib.c \
	gst-controller.c \
	gst-interpolation.c \
	gst-helper.c

libgstcontroller_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS) -I$(top_srcdir)/libs 
libgstcontroller_@GST_MAJORMINOR@_la_LDFLAGS = @GST_LIB_LDFLAGS@
libgstcontroller_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS)
