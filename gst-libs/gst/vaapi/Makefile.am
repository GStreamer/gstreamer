lib_LTLIBRARIES = libgstvaapi-@GST_API_VERSION@.la

if USE_DRM
lib_LTLIBRARIES += libgstvaapi-drm-@GST_API_VERSION@.la
endif

if USE_X11
lib_LTLIBRARIES += libgstvaapi-x11-@GST_API_VERSION@.la
endif

if USE_GLX
lib_LTLIBRARIES += libgstvaapi-glx-@GST_API_VERSION@.la
endif

if USE_WAYLAND
lib_LTLIBRARIES += libgstvaapi-wayland-@GST_API_VERSION@.la
endif

libgstvaapi_includedir = \
	$(includedir)/gstreamer-$(GST_API_VERSION)/gst/vaapi

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
	$(top_builddir)/gst-libs/gst/video/libgstvaapi-videoutils.la \
	$(top_builddir)/gst-libs/gst/codecparsers/libgstvaapi-codecparsers.la

libgstvaapi_source_c =				\
	gstvaapicodec_objects.c			\
	gstvaapicontext.c			\
	gstvaapidecoder.c			\
	gstvaapidecoder_dpb.c			\
	gstvaapidecoder_frame.c			\
	gstvaapidecoder_h264.c			\
	gstvaapidecoder_mpeg2.c			\
	gstvaapidecoder_mpeg4.c			\
	gstvaapidecoder_objects.c		\
	gstvaapidecoder_unit.c			\
	gstvaapidecoder_vc1.c			\
	gstvaapidisplay.c			\
	gstvaapidisplaycache.c			\
	gstvaapiimage.c				\
	gstvaapiimageformat.c			\
	gstvaapiimagepool.c			\
	gstvaapiminiobject.c			\
	gstvaapiobject.c			\
	gstvaapiparamspecs.c			\
	gstvaapiprofile.c			\
	gstvaapisubpicture.c			\
	gstvaapisurface.c			\
	gstvaapisurfacepool.c			\
	gstvaapisurfaceproxy.c			\
	gstvaapiutils.c				\
	gstvaapivalue.c				\
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
	gstvaapivideopool.h			\
	gstvaapiwindow.h			\
	$(NULL)

libgstvaapi_source_priv_h =			\
	glibcompat.h				\
	gstcompat.h				\
	gstvaapi_priv.h				\
	gstvaapicodec_objects.h			\
	gstvaapicompat.h			\
	gstvaapidebug.h				\
	gstvaapidecoder_dpb.h			\
	gstvaapidecoder_frame.h			\
	gstvaapidecoder_objects.h		\
	gstvaapidecoder_priv.h			\
	gstvaapidecoder_unit.h			\
	gstvaapidisplay_priv.h			\
	gstvaapiminiobject.h			\
	gstvaapiobject_priv.h			\
	gstvaapisurface_priv.h			\
	gstvaapiutils.h				\
	gstvaapiversion.h			\
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
	gstvaapiwindow_glx.c			\
	$(NULL)

libgstvaapi_glx_source_h =			\
	gstvaapidisplay_glx.h			\
	gstvaapitexture.h			\
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

libgstvaapi_@GST_API_VERSION@_la_SOURCES =	\
	$(libgstvaapi_source_c)			\
	$(libgstvaapi_source_priv_h)		\
	$(NULL)

libgstvaapi_@GST_API_VERSION@include_HEADERS =	\
	$(libgstvaapi_source_h)			\
	$(NULL)

libgstvaapi_@GST_API_VERSION@includedir =	\
	$(libgstvaapi_includedir)

libgstvaapi_@GST_API_VERSION@_la_CFLAGS =	\
	$(libgstvaapi_cflags)			\
	$(NULL)

libgstvaapi_@GST_API_VERSION@_la_LIBADD =	\
	$(libgstvaapi_libs)			\
	$(NULL)

libgstvaapi_@GST_API_VERSION@_la_LDFLAGS =	\
	$(GST_ALL_LDFLAGS)			\
	$(GST_VAAPI_LT_LDFLAGS)			\
	-export-symbols-regex "^gst_.*vaapi.*"	\
	$(NULL)

libgstvaapi_drm_@GST_API_VERSION@_la_SOURCES =	\
	$(libgstvaapi_drm_source_c)		\
	$(libgstvaapi_drm_source_priv_h)	\
	$(NULL)

libgstvaapi_drm_@GST_API_VERSION@include_HEADERS = \
	$(libgstvaapi_drm_source_h)		\
	$(NULL)

libgstvaapi_drm_@GST_API_VERSION@includedir =	\
	$(libgstvaapi_includedir)

libgstvaapi_drm_@GST_API_VERSION@_la_CFLAGS =	\
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	$(GLIB_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(UDEV_CFLAGS)				\
	$(DRM_CFLAGS)				\
	$(LIBVA_DRM_CFLAGS)			\
	$(NULL)

libgstvaapi_drm_@GST_API_VERSION@_la_LIBADD =	\
	$(GLIB_LIBS)				\
	$(UDEV_LIBS)				\
	$(DRM_LIBS)				\
	$(LIBVA_DRM_LIBS)			\
	libgstvaapi-$(GST_API_VERSION).la	\
	$(NULL)

libgstvaapi_drm_@GST_API_VERSION@_la_LDFLAGS =	\
	$(GST_ALL_LDFLAGS)			\
	$(GST_VAAPI_LT_LDFLAGS)			\
	$(NULL)

libgstvaapi_x11_@GST_API_VERSION@_la_SOURCES =	\
	$(libgstvaapi_x11_source_c)		\
	$(libgstvaapi_x11_source_priv_h)	\
	$(NULL)

libgstvaapi_x11_@GST_API_VERSION@include_HEADERS = \
	$(libgstvaapi_x11_source_h)		\
	$(NULL)

libgstvaapi_x11_@GST_API_VERSION@includedir =	\
	$(libgstvaapi_includedir)

libgstvaapi_x11_@GST_API_VERSION@_la_CFLAGS =	\
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	$(GLIB_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(X11_CFLAGS)				\
	$(XRANDR_CFLAGS)			\
	$(LIBVA_X11_CFLAGS)			\
	$(NULL)

libgstvaapi_x11_@GST_API_VERSION@_la_LIBADD =	\
	$(GLIB_LIBS)				\
	$(X11_LIBS)				\
	$(XRANDR_LIBS)				\
	$(LIBVA_X11_LIBS)			\
	libgstvaapi-$(GST_API_VERSION).la	\
	$(NULL)

libgstvaapi_x11_@GST_API_VERSION@_la_LDFLAGS =	\
	$(GST_ALL_LDFLAGS)			\
	$(GST_VAAPI_LT_LDFLAGS)			\
	$(NULL)

libgstvaapi_glx_@GST_API_VERSION@_la_SOURCES =	\
	$(libgstvaapi_glx_source_c)		\
	$(libgstvaapi_glx_source_priv_h)	\
	$(NULL)

libgstvaapi_glx_@GST_API_VERSION@include_HEADERS = \
	$(libgstvaapi_glx_source_h)		\
	$(NULL)

libgstvaapi_glx_@GST_API_VERSION@includedir =	\
	$(libgstvaapi_includedir)

libgstvaapi_glx_@GST_API_VERSION@_la_CFLAGS =	\
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	$(GLIB_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(GST_VIDEO_CFLAGS)			\
	$(GL_CFLAGS)				\
	$(LIBVA_GLX_CFLAGS)			\
	$(NULL)

libgstvaapi_glx_@GST_API_VERSION@_la_LIBADD =	\
	$(GLIB_LIBS)				\
	$(GST_BASE_LIBS)			\
	$(GST_VIDEO_LIBS)			\
	$(GL_LIBS)				\
	$(LIBVA_GLX_LIBS)			\
	libgstvaapi-x11-$(GST_API_VERSION).la	\
	$(NULL)

libgstvaapi_glx_@GST_API_VERSION@_la_LDFLAGS =	\
	$(GST_ALL_LDFLAGS)			\
	$(GST_VAAPI_LT_LDFLAGS)			\
	$(NULL)

libgstvaapi_wayland_@GST_API_VERSION@_la_SOURCES = \
	$(libgstvaapi_wayland_source_c)		\
	$(libgstvaapi_wayland_source_priv_h)	\
	$(NULL)

libgstvaapi_wayland_@GST_API_VERSION@include_HEADERS = \
	$(libgstvaapi_wayland_source_h)		\
	$(NULL)

libgstvaapi_wayland_@GST_API_VERSION@includedir = \
	$(libgstvaapi_includedir)

libgstvaapi_wayland_@GST_API_VERSION@_la_CFLAGS = \
	-DGST_USE_UNSTABLE_API			\
	-I$(top_srcdir)/gst-libs		\
	$(GLIB_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(WAYLAND_CFLAGS)			\
	$(LIBVA_WAYLAND_CFLAGS)			\
	$(NULL)

libgstvaapi_wayland_@GST_API_VERSION@_la_LIBADD = \
	$(GLIB_LIBS)				\
	$(WAYLAND_LIBS)				\
	$(LIBVA_WAYLAND_LIBS)			\
	libgstvaapi-$(GST_API_VERSION).la	\
	$(NULL)

libgstvaapi_wayland_@GST_API_VERSION@_la_LDFLAGS = \
	$(GST_ALL_LDFLAGS)			\
	$(GST_VAAPI_LT_LDFLAGS)			\
	$(NULL)

VERSION_FILE		= .VERSION
OLD_VERSION_FILE	= $(VERSION_FILE).old
NEW_VERSION_FILE	= $(VERSION_FILE).new
PKG_VERSION_FILE	= $(VERSION_FILE).pkg

gstvaapiversion.h: gen-version
	$(AM_V_GEN)							    \
	OV=`[ -f $(OLD_VERSION_FILE) ] && cat $(OLD_VERSION_FILE) || :`;    \
	NV=`cat $(NEW_VERSION_FILE)`;					    \
	if [ "$$OV" != "$$NV" -o ! -f gstvaapiversion.h ]; then		    \
		cp -f $(NEW_VERSION_FILE) $(OLD_VERSION_FILE);		    \
		$(SED) -e "s|\@GST_VAAPI_VERSION_ID\@|$${NV}|"		    \
			$(srcdir)/gstvaapiversion.h.in > gstvaapiversion.h; \
	fi

gen-version:
	@echo $(VERSION) > $(NEW_VERSION_FILE)
if HAVE_GIT
	@[ -d $(top_srcdir)/.git ] && \
	(cd $(top_srcdir) && $(GIT) describe --tags) > $(NEW_VERSION_FILE) || :
endif
	@[ -f $(srcdir)/$(PKG_VERSION_FILE) ] && \
	cp -f $(srcdir)/$(PKG_VERSION_FILE) $(NEW_VERSION_FILE) || :

$(PKG_VERSION_FILE): $(NEW_VERSION_FILE)
	@cp -f $< $@

BUILT_SOURCES	= gstvaapiversion.h
EXTRA_DIST	= gstvaapiversion.h.in $(PKG_VERSION_FILE)

CLEANFILES = \
	$(OLD_VERSION_FILE)	\
	$(NEW_VERSION_FILE)	\
	$(PKG_VERSION_FILE)	\
	$(NULL)

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in
