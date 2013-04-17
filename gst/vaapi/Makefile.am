plugin_LTLIBRARIES = libgstvaapi.la

libgstvaapi_CFLAGS = \
	$(LIBVA_CFLAGS)			\
	-DGST_USE_UNSTABLE_API		\
	-I$(top_srcdir)/gst-libs	\
	-I$(top_builddir)/gst-libs	\
	$(NULL)

libgstvaapi_LIBS =

if USE_DRM
libgstvaapi_LIBS += \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-drm-$(GST_API_VERSION).la
endif

if USE_X11
libgstvaapi_LIBS += \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-x11-$(GST_API_VERSION).la
endif

if USE_GLX
libgstvaapi_LIBS += \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-glx-$(GST_API_VERSION).la
endif

if USE_WAYLAND
libgstvaapi_LIBS += \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-wayland-$(GST_API_VERSION).la
endif

libgstvaapi_source_c = \
	gstvaapi.c		\
	gstvaapidecode.c	\
	gstvaapipluginutil.c	\
	gstvaapipostproc.c	\
	gstvaapisink.c		\
	gstvaapiuploader.c	\
	gstvaapivideobuffer.c	\
	gstvaapivideometa.c	\
	$(NULL)

libgstvaapi_source_h = \
	gstvaapidecode.h	\
	gstvaapipluginutil.h	\
	gstvaapipostproc.h	\
	gstvaapisink.h		\
	gstvaapiuploader.h	\
	gstvaapivideobuffer.h	\
	gstvaapivideometa.h	\
	$(NULL)

libgstvaapi_glx_source_c = gstvaapivideoconverter_glx.c
libgstvaapi_glx_source_h = gstvaapivideoconverter_glx.h

if USE_GLX
libgstvaapi_source_c += $(libgstvaapi_glx_source_c)
libgstvaapi_source_h += $(libgstvaapi_glx_source_h)
endif

libgstvaapi_1_0p_source_c = \
	gstvaapivideobufferpool.c \
	gstvaapivideomemory.c	\
	$(NULL)

libgstvaapi_1_0p_source_h = \
	gstvaapivideobufferpool.h \
	gstvaapivideomemory.h	\
	$(NULL)

if USE_GST_API_1_0p
libgstvaapi_source_c += $(libgstvaapi_1_0p_source_c)
libgstvaapi_source_h += $(libgstvaapi_1_0p_source_h)
endif

if USE_GST_API_0_10
libgstvaapi_0_10_source_c = \
	gstvaapidownload.c	\
	gstvaapiupload.c	\
	$(NULL)

libgstvaapi_0_10_source_h = \
	gstvaapidownload.h	\
	gstvaapiupload.h	\
	$(NULL)
endif

if USE_GST_API_0_10
libgstvaapi_source_c += $(libgstvaapi_0_10_source_c)
libgstvaapi_source_h += $(libgstvaapi_0_10_source_h)
endif

libgstvaapi_la_SOURCES		= $(libgstvaapi_source_c)
noinst_HEADERS			= $(libgstvaapi_source_h)

libgstvaapi_la_CFLAGS =	\
	$(libgstvaapi_CFLAGS)	\
	$(GST_CFLAGS)		\
	$(GST_BASE_CFLAGS)	\
	$(GST_VIDEO_CFLAGS)	\
	$(GST_INTERFACES_CFLAGS) \
	$(GST_BASEVIDEO_CFLAGS)	\
	$(GST_PLUGINS_BASE_CFLAGS)

libgstvaapi_la_LIBADD =	\
	$(libgstvaapi_LIBS)	\
	$(GST_LIBS)		\
	$(GST_BASE_LIBS)	\
	$(GST_VIDEO_LIBS)	\
	$(GST_INTERFACES_LIBS)	\
	$(GST_BASEVIDEO_LIBS)	\
	$(GST_PLUGINS_BASE_LIBS) \
	$(top_builddir)/gst-libs/gst/video/libgstvaapi-videoutils.la

libgstvaapi_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstvaapi_la_LIBTOOLFLAGS = --tag=disable-static

EXTRA_DIST = \
	$(libgstvaapi_glx_source_c)	\
	$(libgstvaapi_glx_source_h)	\
	$(libgstvaapi_1_0p_source_c)	\
	$(libgstvaapi_1_0p_source_h)	\
	$(libgstvaapi_0_10_source_c)	\
	$(libgstvaapi_0_10_source_h)	\
	$(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
