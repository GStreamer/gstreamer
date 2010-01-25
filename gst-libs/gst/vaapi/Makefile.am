lib_LTLIBRARIES = libgstvaapi-@GST_MAJORMINOR@.la

libgstvaapi_@GST_MAJORMINOR@_la_SOURCES =	\
	gstvaapidisplay.c			\
	vaapi_utils.c				\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@include_HEADERS =	\
	gstvaapidisplay.h			\
	vaapi_debug.h				\
	vaapi_utils.h				\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@includedir = \
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/vaapi

libgstvaapi_@GST_MAJORMINOR@_la_CFLAGS =	\
	$(GST_BASE_CFLAGS)			\
	$(GST_CFLAGS)				\
	$(LIBVA_CFLAGS)				\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@_la_LIBADD =	\
	$(GST_BASE_LIBS)			\
	$(GST_LIBS)				\
	$(LIBVA_LIBS)				\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@_la_SOURCES +=	\
	gstvaapidisplay_x11.c			\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@include_HEADERS +=	\
	gstvaapidisplay_x11.h			\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@_la_CFLAGS +=	\
	$(LIBVA_X11_CFLAGS)			\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@_la_LIBADD +=	\
	$(LIBVA_X11_LIBS)			\
	$(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
