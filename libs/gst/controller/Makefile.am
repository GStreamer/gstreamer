lib_LTLIBRARIES = libgstcontroller-@GST_MAJORMINOR@.la

libgstcontroller_@GST_MAJORMINOR@_includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst/controller
libgstcontroller_@GST_MAJORMINOR@_include_HEADERS = \
	gstcontroller.h

libgstcontroller_@GST_MAJORMINOR@_la_SOURCES = \
	lib.c \
	gstcontroller.c \
	gstinterpolation.c \
	gsthelper.c

libgstcontroller_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstcontroller_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS)
libgstcontroller_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_OBJ_LDFLAGS)
