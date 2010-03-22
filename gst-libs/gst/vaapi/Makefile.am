lib_LTLIBRARIES = 				\
	libgstvaapi-@GST_MAJORMINOR@.la		\
	libgstvaapi-x11-@GST_MAJORMINOR@.la	\
	$(NULL)

libgstvaapi_includedir =			\
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/vaapi

libgstvaapi_source_c =				\
	gstvaapidisplay.c			\
	gstvaapiimage.c				\
	gstvaapiimageformat.c			\
	gstvaapiimagepool.c			\
	gstvaapisubpicture.c			\
	gstvaapisurface.c			\
	gstvaapisurfacepool.c			\
	gstvaapiutils.c				\
	gstvaapivideobuffer.c			\
	gstvaapivideopool.c			\
	gstvaapivideosink.c			\
	gstvaapiwindow.c			\
	$(NULL)

libgstvaapi_source_h =				\
	gstvaapidisplay.h			\
	gstvaapiimage.h				\
	gstvaapiimageformat.h			\
	gstvaapiimagepool.h			\
	gstvaapisubpicture.h			\
	gstvaapisurface.h			\
	gstvaapisurfacepool.h			\
	gstvaapitypes.h				\
	gstvaapivideobuffer.h			\
	gstvaapivideopool.h			\
	gstvaapivideosink.h			\
	gstvaapiwindow.h			\
	$(NULL)

libgstvaapi_source_priv_h =			\
	gstvaapidebug.h				\
	gstvaapiutils.h				\
	gstvaapiwindow_priv.h			\
	$(NULL)

libgstvaapi_x11_source_c =			\
	gstvaapidisplay_x11.c			\
	gstvaapiutils.c				\
	gstvaapiutils_x11.c			\
	gstvaapiwindow_x11.c			\
	$(NULL)

libgstvaapi_x11_source_h =			\
	gstvaapidisplay_x11.h			\
	gstvaapiwindow_x11.h			\
	$(NULL)

libgstvaapi_x11_source_priv_h =			\
	gstvaapiutils.h				\
	gstvaapiutils_x11.h			\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@_la_SOURCES =	\
	$(libgstvaapi_source_c)			\
	$(libgstvaapi_source_priv_h)		\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@include_HEADERS =	\
	$(libgstvaapi_source_h)			\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@includedir =	\
	$(libgstvaapi_includedir)

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

libgstvaapi_x11_@GST_MAJORMINOR@_la_SOURCES =	\
	$(libgstvaapi_x11_source_c)		\
	$(libgstvaapi_x11_source_priv_h)	\
	$(NULL)

libgstvaapi_x11_@GST_MAJORMINOR@include_HEADERS = \
	$(libgstvaapi_x11_source_h)		\
	$(NULL)

libgstvaapi_x11_@GST_MAJORMINOR@includedir =	\
	$(libgstvaapi_includedir)

libgstvaapi_x11_@GST_MAJORMINOR@_la_CFLAGS =	\
	-I$(top_srcdir)/gst-libs		\
	$(GLIB_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(LIBVA_X11_CFLAGS)			\
	$(NULL)

libgstvaapi_x11_@GST_MAJORMINOR@_la_LIBADD =	\
	$(GLIB_LIBS)				\
	$(LIBVA_X11_LIBS)			\
	libgstvaapi-@GST_MAJORMINOR@.la		\
	$(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
