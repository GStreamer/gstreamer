lib_LTLIBRARIES = libgstvaapi-@GST_MAJORMINOR@.la

libgstvaapi_source_c =				\
	gstvaapidisplay.c			\
	gstvaapiimage.c				\
	gstvaapiimageformat.c			\
	gstvaapisubpicture.c			\
	gstvaapisurface.c			\
	vaapi_utils.c				\
	$(NULL)

libgstvaapi_source_h =				\
	gstvaapidisplay.h			\
	gstvaapiimage.h				\
	gstvaapiimageformat.h			\
	gstvaapisubpicture.h			\
	gstvaapisurface.h			\
	vaapi_debug.h				\
	vaapi_utils.h				\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@_la_SOURCES =	\
	$(libgstvaapi_source_c)			\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@include_HEADERS =	\
	$(libgstvaapi_source_h)			\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@includedir =	\
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/vaapi

libgstvaapi_@GST_MAJORMINOR@_la_CFLAGS =	\
	-I$(top_srcdir)/gst-libs		\
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
