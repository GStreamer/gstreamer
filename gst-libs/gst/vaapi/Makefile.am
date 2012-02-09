lib_LTLIBRARIES = 				\
	libgstvaapi-@GST_MAJORMINOR@.la		\
	libgstvaapi-x11-@GST_MAJORMINOR@.la	\
	$(NULL)

if USE_GLX
lib_LTLIBRARIES += libgstvaapi-glx-@GST_MAJORMINOR@.la
endif

libgstvaapi_includedir =			\
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/vaapi

libgstvaapi_cflags =				\
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	$(GST_BASE_CFLAGS)			\
	$(GST_BASEVIDEO_CFLAGS)			\
	$(GST_VIDEO_CFLAGS)			\
	$(GST_CFLAGS)				\
	$(LIBVA_CFLAGS)				\
	$(NULL)

libgstvaapi_libs =				\
	$(GST_BASE_LIBS)			\
	$(GST_BASEVIDEO_LIBS)			\
	$(GST_LIBS)				\
	$(LIBVA_LIBS)				\
	$(NULL)

libgstvaapi_source_c =				\
	gstvaapicontext.c			\
	gstvaapidecoder.c			\
	gstvaapidisplay.c			\
	gstvaapidisplaycache.c			\
	gstvaapiimage.c				\
	gstvaapiimageformat.c			\
	gstvaapiimagepool.c			\
	gstvaapimarshal.c			\
	gstvaapiobject.c			\
	gstvaapiparamspecs.c			\
	gstvaapiprofile.c			\
	gstvaapisubpicture.c			\
	gstvaapisurface.c			\
	gstvaapisurfacepool.c			\
	gstvaapisurfaceproxy.c			\
	gstvaapiutils.c				\
	gstvaapiutils_tsb.c			\
	gstvaapivalue.c				\
	gstvaapivideobuffer.c			\
	gstvaapivideopool.c			\
	gstvaapivideosink.c			\
	gstvaapiwindow.c			\
	$(NULL)

libgstvaapi_source_h =				\
	gstvaapicontext.h			\
	gstvaapidecoder.h			\
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
	gstvaapivideosink.h			\
	gstvaapiwindow.h			\
	$(NULL)

libgstvaapi_source_priv_h =			\
	glibcompat.h				\
	gstvaapi_priv.h				\
	gstvaapicompat.h			\
	gstvaapidebug.h				\
	gstvaapidecoder_priv.h			\
	gstvaapidisplay_priv.h			\
	gstvaapiobject_priv.h			\
	gstvaapisurface_priv.h			\
	gstvaapiutils.h				\
	gstvaapiutils_tsb.h			\
	gstvaapivideobuffer_priv.h		\
	gstvaapiworkarounds.h			\
	sysdeps.h				\
	$(libgst_vaapi_ffmpeg_source_priv_h)	\
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

if USE_FFMPEG
libgstvaapi_source_c	+= gstvaapidecoder_ffmpeg.c
libgstvaapi_source_h	+= gstvaapidecoder_ffmpeg.h
libgstvaapi_cflags	+= $(LIBAVCODEC_CFLAGS)
libgstvaapi_libs	+= $(LIBAVCODEC_LIBS)
endif

if USE_CODEC_PARSERS
libgstvaapi_source_c	+= \
	gstvaapicodec_objects.c			\
	gstvaapidecoder_dpb.c			\
	gstvaapidecoder_h264.c			\
	gstvaapidecoder_mpeg2.c			\
	gstvaapidecoder_mpeg4.c			\
	gstvaapidecoder_objects.c		\
	gstvaapidecoder_vc1.c			\
	$(NULL)
libgstvaapi_source_h	+= \
	gstvaapidecoder_h264.h			\
	gstvaapidecoder_mpeg2.h			\
	gstvaapidecoder_mpeg4.h			\
	gstvaapidecoder_vc1.h			\
	$(NULL)
libgstvaapi_source_priv_h += \
	gstvaapicodec_objects.h			\
	gstvaapidecoder_dpb.h			\
	gstvaapidecoder_objects.h		\
	$(NULL)
libgstvaapi_cflags	+= $(GST_CODEC_PARSERS_CFLAGS)
libgstvaapi_libs	+= $(GST_CODEC_PARSERS_LIBS)

if USE_JPEG_DECODER
libgstvaapi_source_c	+= gstvaapidecoder_jpeg.c
libgstvaapi_source_h	+= gstvaapidecoder_jpeg.h
endif
endif

if USE_LOCAL_CODEC_PARSERS
libgstvaapi_libs += \
	$(top_builddir)/gst-libs/gst/codecparsers/libgstvaapi-codecparsers.la
endif

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
	$(LIBVA_X11_CFLAGS)			\
	$(NULL)

libgstvaapi_x11_@GST_MAJORMINOR@_la_LIBADD =	\
	$(GLIB_LIBS)				\
	$(X11_LIBS)				\
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
	$(GLX_CFLAGS)				\
	$(LIBVA_GLX_CFLAGS)			\
	$(NULL)

libgstvaapi_glx_@GST_MAJORMINOR@_la_LIBADD =	\
	$(GLIB_LIBS)				\
	$(GLX_LIBS)				\
	$(LIBVA_GLX_LIBS)			\
	libgstvaapi-x11-@GST_MAJORMINOR@.la	\
	$(NULL)

libgstvaapi_glx_@GST_MAJORMINOR@_la_LDFLAGS =	\
	$(GST_ALL_LDFLAGS)			\
	$(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in

# glib-genmarshal rules
glib_marshal_list = gstvaapimarshal.list
glib_marshal_prefix = gst_vaapi_marshal

marshal_h = $(glib_marshal_list:.list=.h)
marshal_c = $(glib_marshal_list:.list=.c)

CLEANFILES	= stamp-marshal
DISTCLEANFILES	= $(marshal_h) $(marshal_c)
BUILT_SOURCES	= $(marshal_h) $(marshal_c)
EXTRA_DIST	= $(srcdir)/$(glib_marshal_list)

stamp-marshal: $(glib_marshal_list)
	$(GLIB_GENMARSHAL) \
		--prefix=$(glib_marshal_prefix) \
		--header \
	$(srcdir)/$(glib_marshal_list) > xgen-mh \
	&& (cmp -s xgen-mh $(marshal_h) || cp -f xgen-mh $(marshal_h)) \
	&& rm -f xgen-mh \
	&& echo timestamp > $(@F)

$(marshal_h): stamp-marshal
	@true

$(marshal_c): $(marshal_h)
	(echo "#include \"$(marshal_h)\"" ; \
	$(GLIB_GENMARSHAL) \
		--prefix=$(glib_marshal_prefix) \
		--body \
	$(srcdir)/$(glib_marshal_list)) > xgen-mc \
	&& cp xgen-mc $(marshal_c) \
	&& rm -f xgen-mc
