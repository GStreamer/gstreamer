lib_LTLIBRARIES = libgstvaapi-@GST_MAJORMINOR@.la

if USE_DRM
lib_LTLIBRARIES += libgstvaapi-drm-@GST_MAJORMINOR@.la
endif

if USE_X11
lib_LTLIBRARIES += libgstvaapi-x11-@GST_MAJORMINOR@.la
endif

if USE_GLX
lib_LTLIBRARIES += libgstvaapi-glx-@GST_MAJORMINOR@.la
endif

if USE_WAYLAND
lib_LTLIBRARIES += libgstvaapi-wayland-@GST_MAJORMINOR@.la
endif

libgstvaapi_includedir =			\
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/vaapi

libgstvaapi_cflags =				\
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	-I$(top_builddir)/gst-libs		\
	$(GST_BASE_CFLAGS)			\
	$(GST_BASEVIDEO_CFLAGS)			\
	$(GST_VIDEO_CFLAGS)			\
	$(GST_CFLAGS)				\
	$(GST_CODEC_PARSERS_CFLAGS)		\
	$(LIBVA_CFLAGS)				\
	$(NULL)

libgstvaapi_libs =				\
	$(GST_BASE_LIBS)			\
	$(GST_BASEVIDEO_LIBS)			\
	$(GST_LIBS)				\
	$(GST_VIDEO_LIBS)			\
	$(GST_CODEC_PARSERS_LIBS)		\
	$(LIBVA_LIBS)				\
	$(top_builddir)/gst-libs/gst/codecparsers/libgstvaapi-codecparsers.la

libgstvaapi_source_c =				\
	gstvaapicodec_objects.c			\
	gstvaapicontext.c			\
	gstvaapidecoder.c			\
	gstvaapidecoder_dpb.c			\
	gstvaapidecoder_h264.c			\
	gstvaapidecoder_mpeg2.c			\
	gstvaapidecoder_mpeg4.c			\
	gstvaapidecoder_objects.c		\
	gstvaapidecoder_vc1.c			\
	gstvaapidisplay.c			\
	gstvaapidisplaycache.c			\
	gstvaapiimage.c				\
	gstvaapiimageformat.c			\
	gstvaapiimagepool.c			\
	gstvaapiobject.c			\
	gstvaapiparamspecs.c			\
	gstvaapiprofile.c			\
	gstvaapisubpicture.c			\
	gstvaapisurface.c			\
	gstvaapisurfacepool.c			\
	gstvaapisurfaceproxy.c			\
	gstvaapiutils.c				\
	gstvaapivalue.c				\
	gstvaapivideobuffer.c			\
	gstvaapivideopool.c			\
	gstvaapiwindow.c			\
	$(NULL)

libgstvaapi_source_h =				\
	gstvaapicontext.h			\
	gstvaapidecoder.h			\
	gstvaapidecoder_h264.h			\
	gstvaapidecoder_mpeg2.h			\
	gstvaapidecoder_mpeg4.h			\
	gstvaapidecoder_vc1.h			\
	gstvaapidisplay.h			\
	gstvaapidisplaycache.h			\
	gstvaapiimage.h				\
	gstvaapiimageformat.h			\
	gstvaapiimagepool.h			\
	gstvaapiobject.h			\
	gstvaapiparamspecs.h			\
	gstvaapiprofile.h			\
	gstvaapisubpicture.h			\
	gstvaapisurface.h			\
	gstvaapisurfacepool.h			\
	gstvaapisurfaceproxy.h			\
	gstvaapitypes.h				\
	gstvaapivalue.h				\
	gstvaapivideobuffer.h			\
	gstvaapivideopool.h			\
	gstvaapiwindow.h			\
	$(NULL)

libgstvaapi_source_priv_h =			\
	glibcompat.h				\
	gstvaapi_priv.h				\
	gstvaapicodec_objects.h			\
	gstvaapicompat.h			\
	gstvaapidebug.h				\
	gstvaapidecoder_dpb.h			\
	gstvaapidecoder_objects.h		\
	gstvaapidecoder_priv.h			\
	gstvaapidisplay_priv.h			\
	gstvaapiobject_priv.h			\
	gstvaapisurface_priv.h			\
	gstvaapiutils.h				\
	gstvaapivideobuffer_priv.h		\
	gstvaapiworkarounds.h			\
	sysdeps.h				\
	$(NULL)

if USE_JPEG_DECODER
libgstvaapi_source_c += gstvaapidecoder_jpeg.c
libgstvaapi_source_h += gstvaapidecoder_jpeg.h
endif

libgstvaapi_drm_source_c =			\
	gstvaapidisplay_drm.c			\
	gstvaapiwindow_drm.c			\
	gstvaapiutils.c				\
	$(NULL)

libgstvaapi_drm_source_h =			\
	gstvaapidisplay_drm.h			\
	gstvaapiwindow_drm.h			\
	$(NULL)

libgstvaapi_drm_source_priv_h =			\
	gstvaapicompat.h			\
	gstvaapidisplay_drm_priv.h		\
	gstvaapiutils.h				\
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
	gstvaapicompat.h			\
	gstvaapidisplay_x11_priv.h		\
	gstvaapiutils.h				\
	gstvaapiutils_x11.h			\
	$(NULL)

libgstvaapi_glx_source_c =			\
	gstvaapidisplay_glx.c			\
	gstvaapitexture.c			\
	gstvaapiutils.c				\
	gstvaapiutils_glx.c			\
	gstvaapiutils_x11.c			\
	gstvaapivideobuffer_glx.c		\
	gstvaapivideoconverter_glx.c		\
	gstvaapiwindow_glx.c			\
	$(NULL)

libgstvaapi_glx_source_h =			\
	gstvaapidisplay_glx.h			\
	gstvaapitexture.h			\
	gstvaapivideobuffer_glx.h		\
	gstvaapivideoconverter_glx.h		\
	gstvaapiwindow_glx.h			\
	$(NULL)

libgstvaapi_glx_source_priv_h =			\
	gstvaapicompat.h			\
	gstvaapidisplay_glx_priv.h		\
	gstvaapiutils.h				\
	gstvaapiutils_glx.h			\
	gstvaapiutils_x11.h			\
	$(NULL)

libgstvaapi_wayland_source_c =			\
	gstvaapidisplay_wayland.c		\
	gstvaapiutils.c				\
	gstvaapiwindow_wayland.c		\
	$(NULL)

libgstvaapi_wayland_source_h =			\
	gstvaapidisplay_wayland.h		\
	gstvaapiwindow_wayland.h		\
	$(NULL)

libgstvaapi_wayland_source_priv_h =		\
	gstvaapicompat.h			\
	gstvaapidisplay_wayland_priv.h		\
	gstvaapiutils.h				\
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
	$(libgstvaapi_cflags)			\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@_la_LIBADD =	\
	$(libgstvaapi_libs)			\
	$(NULL)

libgstvaapi_@GST_MAJORMINOR@_la_LDFLAGS =	\
	$(GST_ALL_LDFLAGS)			\
	$(NULL)

libgstvaapi_drm_@GST_MAJORMINOR@_la_SOURCES =	\
	$(libgstvaapi_drm_source_c)		\
	$(libgstvaapi_drm_source_priv_h)	\
	$(NULL)

libgstvaapi_drm_@GST_MAJORMINOR@include_HEADERS = \
	$(libgstvaapi_drm_source_h)		\
	$(NULL)

libgstvaapi_drm_@GST_MAJORMINOR@includedir =	\
	$(libgstvaapi_includedir)

libgstvaapi_drm_@GST_MAJORMINOR@_la_CFLAGS =	\
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	$(GLIB_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(UDEV_CFLAGS)				\
	$(DRM_CFLAGS)				\
	$(LIBVA_DRM_CFLAGS)			\
	$(NULL)

libgstvaapi_drm_@GST_MAJORMINOR@_la_LIBADD =	\
	$(GLIB_LIBS)				\
	$(UDEV_LIBS)				\
	$(DRM_LIBS)				\
	$(LIBVA_DRM_LIBS)			\
	libgstvaapi-@GST_MAJORMINOR@.la		\
	$(NULL)

libgstvaapi_drm_@GST_MAJORMINOR@_la_LDFLAGS =	\
	$(GST_ALL_LDFLAGS)			\
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
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	$(GLIB_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(X11_CFLAGS)				\
	$(XRANDR_CFLAGS)			\
	$(LIBVA_X11_CFLAGS)			\
	$(NULL)

libgstvaapi_x11_@GST_MAJORMINOR@_la_LIBADD =	\
	$(GLIB_LIBS)				\
	$(X11_LIBS)				\
	$(XRANDR_LIBS)				\
	$(LIBVA_X11_LIBS)			\
	libgstvaapi-@GST_MAJORMINOR@.la		\
	$(NULL)

libgstvaapi_x11_@GST_MAJORMINOR@_la_LDFLAGS =	\
	$(GST_ALL_LDFLAGS)			\
	$(NULL)

libgstvaapi_glx_@GST_MAJORMINOR@_la_SOURCES =	\
	$(libgstvaapi_glx_source_c)		\
	$(libgstvaapi_glx_source_priv_h)	\
	$(NULL)

libgstvaapi_glx_@GST_MAJORMINOR@include_HEADERS = \
	$(libgstvaapi_glx_source_h)		\
	$(NULL)

libgstvaapi_glx_@GST_MAJORMINOR@includedir =	\
	$(libgstvaapi_includedir)

libgstvaapi_glx_@GST_MAJORMINOR@_la_CFLAGS =	\
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	$(GLIB_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(GL_CFLAGS)				\
	$(LIBVA_GLX_CFLAGS)			\
	$(NULL)

libgstvaapi_glx_@GST_MAJORMINOR@_la_LIBADD =	\
	$(GLIB_LIBS)				\
	$(GL_LIBS)				\
	$(LIBVA_GLX_LIBS)			\
	libgstvaapi-x11-@GST_MAJORMINOR@.la	\
	$(NULL)

libgstvaapi_glx_@GST_MAJORMINOR@_la_LDFLAGS =	\
	$(GST_ALL_LDFLAGS)			\
	$(NULL)

libgstvaapi_wayland_@GST_MAJORMINOR@_la_SOURCES = \
	$(libgstvaapi_wayland_source_c)		\
	$(libgstvaapi_wayland_source_priv_h)	\
	$(NULL)

libgstvaapi_wayland_@GST_MAJORMINOR@include_HEADERS = \
	$(libgstvaapi_wayland_source_h)		\
	$(NULL)

libgstvaapi_wayland_@GST_MAJORMINOR@includedir = \
	$(libgstvaapi_includedir)

libgstvaapi_wayland_@GST_MAJORMINOR@_la_CFLAGS = \
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	$(GLIB_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(WAYLAND_CFLAGS)			\
	$(LIBVA_WAYLAND_CFLAGS)			\
	$(NULL)

libgstvaapi_wayland_@GST_MAJORMINOR@_la_LIBADD = \
	$(GLIB_LIBS)				\
	$(WAYLAND_LIBS)				\
	$(LIBVA_WAYLAND_LIBS)			\
	libgstvaapi-@GST_MAJORMINOR@.la		\
	$(NULL)

libgstvaapi_wayland_@GST_MAJORMINOR@_la_LDFLAGS = \
	$(GST_ALL_LDFLAGS)			\
	$(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
