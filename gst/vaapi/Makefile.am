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
libgstvaapi_CFLAGS += \
	$(WAYLAND_CFLAGS)	\
	$(NULL)

libgstvaapi_LIBS += \
	$(top_builddir)/gst-libs/gst/vaapi/libgstvaapi-wayland-$(GST_API_VERSION).la
endif

libgstvaapi_source_c = \
	gstvaapi.c		\
	gstvaapidecode.c	\
	gstvaapipluginbase.c	\
	gstvaapipluginutil.c	\
	gstvaapipostproc.c	\
	gstvaapisink.c		\
	gstvaapiuploader.c	\
	gstvaapivideobuffer.c	\
	gstvaapivideocontext.c	\
	gstvaapivideometa.c	\
	$(NULL)

libgstvaapi_source_h = \
	gstvaapidecode.h	\
	gstvaapipluginbase.h	\
	gstvaapipluginutil.h	\
	gstvaapipostproc.h	\
	gstvaapisink.h		\
	gstvaapiuploader.h	\
	gstvaapivideobuffer.h	\
	gstvaapivideocontext.h	\
	gstvaapivideometa.h	\
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

libgstvaapi_x11_source_c = gstvaapivideoconverter_x11.c
libgstvaapi_x11_source_h = gstvaapivideoconverter_x11.h

if USE_X11
if !USE_GST_API_1_2p
libgstvaapi_source_c += $(libgstvaapi_x11_source_c)
libgstvaapi_source_h += $(libgstvaapi_x11_source_h)
endif
endif

libgstvaapi_glx_source_c = gstvaapivideoconverter_glx.c
libgstvaapi_glx_source_h = gstvaapivideoconverter_glx.h

if USE_GLX
if !USE_GST_API_1_2p
libgstvaapi_source_c += $(libgstvaapi_glx_source_c)
libgstvaapi_source_h += $(libgstvaapi_glx_source_h)
endif
endif

libgstvaapi_1_2p_source_c = \
	gstvaapivideometa_texture.c \
	$(NULL)

libgstvaapi_1_2p_source_h = \
	gstvaapivideometa_texture.h \
	$(NULL)

if USE_GST_API_1_2p
libgstvaapi_source_c += $(libgstvaapi_1_2p_source_c)
libgstvaapi_source_h += $(libgstvaapi_1_2p_source_h)
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

libgstvaapi_0_10_source_c = \
	gstvaapidownload.c	\
	gstvaapiupload.c	\
	$(NULL)

libgstvaapi_0_10_source_h = \
	gstvaapidownload.h	\
	gstvaapiupload.h	\
	$(NULL)

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

BUILT_SOURCES =

if USE_LOCAL_VIDEO_PARSERS
plugin_LTLIBRARIES += libgstvaapi_parse.la

libgstvaapi_parse_gen_source_c = \
	gsth264parse.c		\
	$(NULL)

libgstvaapi_parse_gen_source_h = \
	gsth264parse.h		\
	$(NULL)

libgstvaapi_parse_gen_sources = \
	$(libgstvaapi_parse_gen_source_c) \
	$(libgstvaapi_parse_gen_source_h) \
	$(NUL)

libgstvaapi_parse_source_c = gstvaapiparse.c $(libgstvaapi_parse_gen_source_c)
libgstvaapi_parse_source_h = gstvaapiparse.h $(libgstvaapi_parse_gen_source_h)

libgstvaapi_parse_la_SOURCES		= $(libgstvaapi_parse_source_c)
noinst_HEADERS			       += $(libgstvaapi_parse_source_h)

libgstvaapi_parse_la_CFLAGS = \
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	-I$(top_builddir)/gst-libs		\
	$(GST_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(GST_PLUGINS_BASE_CFLAGS)		\
	$(GST_VIDEO_CFLAGS)

libgstvaapi_parse_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/codecparsers/libgstvaapi-codecparsers.la \
	$(GST_LIBS)				\
	$(GST_BASE_LIBS)			\
	$(GST_PLUGINS_BASE_LIBS)		\
	$(GST_VIDEO_LIBS) -lgstpbutils-$(GST_PKG_VERSION)

libgstvaapi_parse_la_LDFLAGS		= $(GST_PLUGIN_LDFLAGS)
libgstvaapi_parse_la_LIBTOOLFLAGS	= --tag=disable-static

videoparsers_sources_dir = \
	$(top_srcdir)/ext/codecparsers/gst/videoparsers
videoparsers_patches_dir = \
	$(top_srcdir)/patches/videoparsers
include $(videoparsers_patches_dir)/series.frag
videoparsers_patches = \
	$(videoparsers_patches_base:%=$(top_srcdir)/patches/videoparsers/%)

videoparsers.prepare.stamp: $(videoparsers_patches)
	@for f in $(libgstvaapi_parse_gen_sources); do	\
	  cp -f $(videoparsers_sources_dir)/$$f $$f;	\
	done
	@for f in $(videoparsers_patches); do		\
	  patch -p3 < $$f;				\
	done
	@touch $@

BUILT_SOURCES += videoparsers.prepare.stamp
endif

CLEANFILES = \
	videoparsers.prepare.stamp	\
	$(libgstvaapi_parse_gen_sources)

EXTRA_DIST = \
	$(libgstvaapi_enc_source_c)	\
	$(libgstvaapi_enc_source_h)	\
	$(libgstvaapi_x11_source_c)	\
	$(libgstvaapi_x11_source_h)	\
	$(libgstvaapi_glx_source_c)	\
	$(libgstvaapi_glx_source_h)	\
	$(libgstvaapi_1_2p_source_c)	\
	$(libgstvaapi_1_2p_source_h)	\
	$(libgstvaapi_1_0p_source_c)	\
	$(libgstvaapi_1_0p_source_h)	\
	$(libgstvaapi_0_10_source_c)	\
	$(libgstvaapi_0_10_source_h)	\
	$(libgstvaapi_parse_source_c)	\
	$(libgstvaapi_parse_source_h)	\
	$(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
