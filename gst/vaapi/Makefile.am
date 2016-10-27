plugin_LTLIBRARIES = libgstvaapi.la

libgstvaapi_CFLAGS = \
	$(LIBVA_CFLAGS)			\
	-DGST_USE_UNSTABLE_API		\
	-I$(top_srcdir)/gst-libs	\
	-I$(top_builddir)/gst-libs	\
	$(NULL)

libgstvaapi_LIBS = $(top_builddir)/gst-libs/gst/vaapi/libgstvaapi.la

if USE_DRM
libgstvaapi_LIBS +=$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-drm.la
endif

if USE_X11
libgstvaapi_LIBS += \
	$(X11_LIBS) \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-x11.la
endif

if USE_GLX
libgstvaapi_LIBS += $(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-glx.la
endif

if USE_EGL
libgstvaapi_LIBS += $(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-egl.la
endif

if USE_WAYLAND
libgstvaapi_CFLAGS += \
	$(WAYLAND_CFLAGS)	\
	$(NULL)

libgstvaapi_LIBS += $(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-wayland.la
endif

if USE_GST_GL_HELPERS
libgstvaapi_CFLAGS	+= $(GST_GL_CFLAGS)
libgstvaapi_LIBS	+= $(GST_GL_LIBS)
endif

libgstvaapi_source_c = \
	gstvaapi.c		\
	gstvaapidecode.c	\
	gstvaapidecodedoc.c	\
	gstvaapipluginbase.c	\
	gstvaapipluginutil.c	\
	gstvaapipostproc.c	\
	gstvaapipostprocutil.c	\
	gstvaapisink.c		\
	gstvaapivideobuffer.c	\
	gstvaapivideocontext.c	\
	gstvaapivideometa.c	\
	gstvaapidecodebin.c	\
	gstvaapivideobufferpool.c \
	gstvaapivideomemory.c	\
	gstvaapivideometa_texture.c \
	$(NULL)

libgstvaapi_source_h = \
	gstcompat.h		\
	gstvaapidecode.h	\
	gstvaapipluginbase.h	\
	gstvaapipluginutil.h	\
	gstvaapipostproc.h	\
	gstvaapipostprocutil.h	\
	gstvaapisink.h		\
	gstvaapivideobuffer.h	\
	gstvaapivideocontext.h	\
	gstvaapivideometa.h	\
	gstvaapidecodebin.h	\
	gstvaapivideobufferpool.h \
	gstvaapivideomemory.h	\
	gstvaapivideometa_texture.h \
	$(NULL)

libgstvaapi_enc_source_c =	\
	gstvaapiencode.c	\
	gstvaapiencode_h264.c	\
	gstvaapiencode_mpeg2.c	\
	$(NULL)

libgstvaapi_enc_source_h =	\
	gstvaapiencode.h	\
	gstvaapiencode_h264.h	\
	gstvaapiencode_mpeg2.h	\
	$(NULL)

if USE_ENCODERS
libgstvaapi_source_c += $(libgstvaapi_enc_source_c)
libgstvaapi_source_h += $(libgstvaapi_enc_source_h)
endif

libgstvaapi_jpegenc_source_c = gstvaapiencode_jpeg.c
libgstvaapi_jpegenc_source_h = gstvaapiencode_jpeg.h
if USE_JPEG_ENCODER
libgstvaapi_source_c += $(libgstvaapi_jpegenc_source_c)
libgstvaapi_source_h += $(libgstvaapi_jpegenc_source_h)
endif

libgstvaapi_vp8enc_source_c = gstvaapiencode_vp8.c
libgstvaapi_vp8enc_source_h = gstvaapiencode_vp8.h
if USE_VP8_ENCODER
libgstvaapi_source_c += $(libgstvaapi_vp8enc_source_c)
libgstvaapi_source_h += $(libgstvaapi_vp8enc_source_h)
endif

libgstvaapi_h265enc_source_c = gstvaapiencode_h265.c
libgstvaapi_h265enc_source_h = gstvaapiencode_h265.h
if USE_H265_ENCODER
libgstvaapi_source_c += $(libgstvaapi_h265enc_source_c)
libgstvaapi_source_h += $(libgstvaapi_h265enc_source_h)
endif

libgstvaapi_vp9enc_source_c = gstvaapiencode_vp9.c
libgstvaapi_vp9enc_source_h = gstvaapiencode_vp9.h
if USE_VP9_ENCODER
libgstvaapi_source_c += $(libgstvaapi_vp9enc_source_c)
libgstvaapi_source_h += $(libgstvaapi_vp9enc_source_h)
endif

libgstvaapi_la_SOURCES		= $(libgstvaapi_source_c)
noinst_HEADERS			= $(libgstvaapi_source_h)

libgstvaapi_la_CFLAGS =	\
	$(libgstvaapi_CFLAGS)	\
	$(GST_CFLAGS)		\
	$(GST_BASE_CFLAGS)	\
	$(GST_VIDEO_CFLAGS)	\
	$(GST_PBUTILS_CFLAGS)	\
	$(GST_INTERFACES_CFLAGS) \
	$(GST_BASEVIDEO_CFLAGS)	\
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_ALLOCATORS_CFLAGS)

libgstvaapi_la_LIBADD =	\
	$(libgstvaapi_LIBS)	\
	$(GST_LIBS)		\
	$(GST_BASE_LIBS)	\
	$(GST_VIDEO_LIBS)	\
	$(GST_PBUTILS_LIBS)	\
	$(GST_INTERFACES_LIBS)	\
	$(GST_BASEVIDEO_LIBS)	\
	$(GST_PLUGINS_BASE_LIBS) \
	$(GST_ALLOCATORS_LIBS)

libgstvaapi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvaapi_la_LIBTOOLFLAGS = --tag=disable-static

EXTRA_DIST = \
	$(libgstvaapi_enc_source_c)	 \
	$(libgstvaapi_enc_source_h)	 \
	$(libgstvaapi_jpegenc_source_c)	 \
	$(libgstvaapi_jpegenc_source_h)  \
	$(libgstvaapi_vp8enc_source_c)   \
	$(libgstvaapi_vp8enc_source_h)   \
	$(libgstvaapi_h265enc_source_c)  \
	$(libgstvaapi_h265enc_source_h)  \
	$(libgstvaapi_vp9enc_source_c)   \
	$(libgstvaapi_vp9enc_source_h)   \
	$(libgstvaapi_egl_source_c)	\
	$(libgstvaapi_egl_source_h)	\
	$(libgstvaapi_1_2p_source_c)	 \
	$(libgstvaapi_1_2p_source_h)	 \
	$(NULL)

-include $(top_srcdir)/git.mk
