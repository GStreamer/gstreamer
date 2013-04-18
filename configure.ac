# gstreamer-vaapi package version number
m4_define([gst_vaapi_major_version], [0])
m4_define([gst_vaapi_minor_version], [5])
m4_define([gst_vaapi_micro_version], [3])
m4_define([gst_vaapi_pre_version],   [0])
m4_define([gst_vaapi_version],
          [gst_vaapi_major_version.gst_vaapi_minor_version.gst_vaapi_micro_version])
m4_if(gst_vaapi_pre_version, [0], [], [
m4_append([gst_vaapi_version], gst_vaapi_pre_version, [.pre])
])

# gstreamer-vaapi library (libtool) version number
m4_define([gst_vaapi_lt_current],       [2])
m4_define([gst0_vaapi_lt_current_bias], [0])
m4_define([gst1_vaapi_lt_current_bias], [2])
m4_define([gst_vaapi_lt_revision],      [0])
m4_define([gst_vaapi_lt_age],           [0])

# glib version number
m4_define([glib_version], [2.28])

# gstreamer version number
m4_define([gst_api_version],            [1.0])
m4_define([gst0_version],               [0.10.36])
m4_define([gst0_plugins_base_version],  [0.10.31])
m4_define([gst0_plugins_bad_version],   [0.10.22])
m4_define([gst1_version],               [1.0.0])
m4_define([gst1_plugins_base_version],  [1.0.0])
m4_define([gst1_plugins_bad_version],   [1.0.0])

# Wayland minimum version number
m4_define([wayland_api_version], [1.0.0])

# VA-API minimum version number
m4_define([va_api_version],     [0.30.4])
m4_define([va_api_drm_version], [0.33.0])
m4_define([va_api_x11_version], [0.31.0])
m4_define([va_api_glx_version], [0.32.0])
m4_define([va_api_wld_version], [0.33.0])

# libva package version number
m4_define([libva_drm_package_version], [1.1.0])
m4_define([libva_x11_package_version], [1.0.3])
m4_define([libva_glx_package_version], [1.0.9])
m4_define([libva_wld_package_version], [1.1.0])

# gtk-doc version number
# XXX: introspection annotations require gtk-doc >= 1.12
m4_define([gtkdoc_version], [1.9])

AC_PREREQ([2.66])
AC_INIT([gst_vaapi], [gst_vaapi_version],
    [gwenole.beauchesne@intel.com],
    [gstreamer-vaapi])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([1.11 tar-ustar no-dist-gzip dist-bzip2])

TODAY="`LC_ALL=C date +'%a, %d %b %Y %X %z'`"
AC_SUBST(TODAY)

LIBVA_PACKAGE_VERSION=libva_x11_package_version
AC_SUBST(LIBVA_PACKAGE_VERSION)

WAYLAND_API_VERSION=wayland_api_version
AC_SUBST(WAYLAND_API_VERSION)

dnl Use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])], [
  AM_DEFAULT_VERBOSITY=1
  AC_SUBST(AM_DEFAULT_VERBOSITY)
])

dnl Check for tools
AC_PROG_CC
AM_PROG_CC_C_O

AC_ARG_VAR([GIT], [Path to git program, if any])
AC_PATH_PROG([GIT], [git])
AM_CONDITIONAL([HAVE_GIT], [test -n "$GIT"])

dnl Initialize libtool
LT_PREREQ([2.2])
LT_INIT

AC_ARG_ENABLE(drm,
    AS_HELP_STRING([--enable-drm],
                   [enable DRM backend @<:@default=yes@:>@]),
    [], [enable_drm="yes"])

AC_ARG_ENABLE(x11,
    AS_HELP_STRING([--enable-x11],
                   [enable X11 output @<:@default=yes@:>@]),
    [], [enable_x11="yes"])

AC_ARG_ENABLE(glx,
    AS_HELP_STRING([--enable-glx],
                   [enable OpenGL/X11 output @<:@default=yes@:>@]),
    [], [enable_glx="yes"])

AC_ARG_ENABLE(wayland,
    AC_HELP_STRING([--enable-wayland],
                   [enable Wayland output @<:@default=yes@:>@]),
    [], [enable_wayland="yes"])

AC_ARG_WITH([gstreamer-api],
    AC_HELP_STRING([--with-gstreamer-api=VERSION],
                   [build against the specified GStreamer API version
                    @<:@default=gst_api_version@:>@]),
    [GST_API_VERSION="$with_gstreamer_api"], [GST_API_VERSION=gst_api_version])

dnl Check for basic libraries
AC_CHECK_LIB(m, tan)

dnl Check for Gtk doc
GTKDOC_VERSION=gtkdoc_version
# gtkdocize greps for '^GTK_DOC_CHECK', so it needs to be on its own line
m4_ifdef([GTK_DOC_CHECK], [
GTK_DOC_CHECK([$GTKDOC_VERSION], [--flavour no-tmpl])], [
AM_CONDITIONAL([ENABLE_GTK_DOC], [false])])
AC_SUBST(GTKDOC_VERSION)

dnl Check for GLib
GLIB_VERSION_REQUIRED=glib_version
PKG_CHECK_MODULES([GLIB], [glib-2.0 >= $GLIB_VERSION_REQUIRED])
AC_SUBST(GLIB_VERSION_REQUIRED)

dnl Check to see if dlopen is in default libraries (like Solaris, which
dnl has it in libc), or if libdl is needed to get it.
AC_CHECK_FUNC([dlopen], [], [
    AC_CHECK_LIB([dl], [dlopen], [DLOPEN_LIBS="-ldl"])])
AC_SUBST([DLOPEN_LIBS])

dnl ---------------------------------------------------------------------------
dnl -- GStreamer                                                             --
dnl ---------------------------------------------------------------------------

dnl Versions for GStreamer and plugins-base
case $GST_API_VERSION in
0.10)
    GST_VERSION_REQUIRED=gst0_version
    GST_PLUGINS_BASE_VERSION_REQUIRED=gst0_plugins_base_version
    GST_PLUGINS_BAD_VERSION_REQUIRED=gst0_plugins_bad_version
    ;;
1.0)
    GST_VERSION_REQUIRED=gst1_version
    GST_PLUGINS_BASE_VERSION_REQUIRED=gst1_plugins_base_version
    GST_PLUGINS_BAD_VERSION_REQUIRED=gst1_plugins_bad_version
    ;;
*)
    AC_MSG_ERROR([unsupported GStreamer API version $GST_API_VERSION])
    ;;
esac
AC_SUBST(GST_API_VERSION)
AC_SUBST(GST_VERSION_REQUIRED)
AC_SUBST(GST_PLUGINS_BASE_VERSION_REQUIRED)
AC_SUBST(GST_PLUGINS_BAD_VERSION_REQUIRED)

USE_GST_API_0_10="no"
USE_GST_API_1_0p="no"
AS_VERSION_COMPARE([$GST_API_VERSION], [0.10],
    [], [USE_GST_API_0_10="yes"], [])
AS_VERSION_COMPARE([$GST_API_VERSION], [1.0],
    [], [USE_GST_API_1_0p="yes"], [USE_GST_API_1_0p="yes"])
AM_CONDITIONAL([USE_GST_API_0_10], [test "$USE_GST_API_0_10" = "yes"])
AM_CONDITIONAL([USE_GST_API_1_0p], [test "$USE_GST_API_1_0p" = "yes"])

dnl GStreamer Core
PKG_CHECK_MODULES([GST],
    [gstreamer-$GST_API_VERSION >= $GST_VERSION_REQUIRED])
PKG_CHECK_MODULES([GST_BASE],
    [gstreamer-base-$GST_API_VERSION >= $GST_VERSION_REQUIRED])

AC_CACHE_CHECK([for GstBaseSink::query hook], ac_cv_have_gst_base_sink_query, [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $GST_BASE_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $GST_BASE_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <gst/base/gstbasesink.h>]],
            [[GstBaseSinkClass klass; klass.query = NULL;]])],
        [ac_cv_have_gst_base_sink_query="yes"],
        [ac_cv_have_gst_base_sink_query="no"]
    )
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
])
if test "$ac_cv_have_gst_base_sink_query" != "yes"; then
    AC_MSG_ERROR([GstBaseSink does not contain the 'query' vfunc])
fi

dnl GStreamer -base plugins
PKG_CHECK_MODULES([GST_PLUGINS_BASE],
    [gstreamer-plugins-base-$GST_API_VERSION >= $GST_PLUGINS_BASE_VERSION_REQUIRED])
if test "$GST_API_VERSION" = "0.10"; then
PKG_CHECK_MODULES([GST_INTERFACES],
    [gstreamer-interfaces-$GST_API_VERSION >= $GST_PLUGINS_BASE_VERSION_REQUIRED])
fi

dnl ... GstVideoOverlayComposition (gstreamer-video)
PKG_CHECK_MODULES([GST_VIDEO],
    [gstreamer-video-$GST_API_VERSION >= $GST_PLUGINS_BASE_VERSION_REQUIRED])

AC_CACHE_CHECK([for GstVideoOverlayComposition],
    ac_cv_have_gst_video_overlay_composition, [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $GST_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $GST_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <gst/video/video-overlay-composition.h>]],
            [[GstVideoOverlayComposition *c;
              c = gst_video_overlay_composition_new(0);]])],
        [ac_cv_have_gst_video_overlay_composition="yes"],
        [ac_cv_have_gst_video_overlay_composition="no"]
    )
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
])
if test "$ac_cv_have_gst_video_overlay_composition" != "yes"; then
    AC_MSG_ERROR([GstVideoOverlayComposition is not available])
fi

AC_CACHE_CHECK([... with advanced format flags],
    ac_cv_have_gst_video_overlay_hwcaps, [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $GST_CFLAGS $GST_VIDEO_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $GST_LIBS $GST_VIDEO_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <gst/video/video-overlay-composition.h>]],
            [[GstVideoOverlayRectangle *rect;
              const guint w = 64, h = 64;
              guint flags = (
                  GST_VIDEO_OVERLAY_FORMAT_FLAG_PREMULTIPLIED_ALPHA|
                  GST_VIDEO_OVERLAY_FORMAT_FLAG_GLOBAL_ALPHA);
              rect = gst_video_overlay_rectangle_new_argb(NULL, w, h, w * 4,
                  0, 0, w, h, flags);
              gst_video_overlay_rectangle_set_global_alpha(rect, 0.5f);]])],
        [ac_cv_have_gst_video_overlay_hwcaps="yes"],
        [ac_cv_have_gst_video_overlay_hwcaps="no"]
    )
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
])
if test "$ac_cv_have_gst_video_overlay_hwcaps" = "yes"; then
    AC_DEFINE_UNQUOTED([HAVE_GST_VIDEO_OVERLAY_HWCAPS], 1,
        [Defined to 1 if GstVideoOverlayComposition API supports HW hints.])
fi

dnl ... GstVideoDecoder (gstreamer-video)
AC_CACHE_CHECK([for GstVideoDecoder],
    ac_cv_have_gst_video_decoder, [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $GST_CFLAGS $GST_VIDEO_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $GST_LIBS $GST_VIDEO_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <gst/video/gstvideodecoder.h>]],
            [[GstVideoCodecFrame *f;
              GstVideoDecoder vdec;
              f = g_slice_new0(GstVideoCodecFrame);
              f->ref_count = 1;
              gst_video_decoder_have_frame(&vdec);
              gst_video_decoder_finish_frame(&vdec, f);
              gst_video_codec_frame_unref(f);]])],
        [ac_cv_have_gst_video_decoder="yes"],
        [ac_cv_have_gst_video_decoder="no"]
    )
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
])
AM_CONDITIONAL([USE_LOCAL_GST_VIDEO_DECODER],
    [test "$ac_cv_have_gst_video_decoder" != "yes"])

dnl GStreamer -bad plugins
PKG_CHECK_MODULES([GST_BASEVIDEO],
    [gstreamer-basevideo-$GST_API_VERSION >= $GST_PLUGINS_BAD_VERSION_REQUIRED])

dnl ... bitstream parsers
PKG_CHECK_MODULES([GST_CODEC_PARSERS],
    [gstreamer-codecparsers-$GST_API_VERSION >= $GST_PLUGINS_BAD_VERSION_REQUIRED])

dnl ... MPEG-2 parser, with the required extensions
AC_CACHE_CHECK([for MPEG-2 parser],
    ac_cv_have_gst_mpeg2_parser, [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $GST_CFLAGS $GST_CODEC_PARSERS_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $GST_LIBS $GST_CODEC_PARSERS_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <gst/codecparsers/gstmpegvideoparser.h>]],
            [[GstMpegVideoSequenceHdr seq_hdr;
              GstMpegVideoSequenceExt seq_ext;
              GstMpegVideoSequenceDisplayExt seq_dpy;
              gst_mpeg_video_finalise_mpeg2_sequence_header(&seq_hdr,
                  &seq_ext, &seq_dpy);]])],
        [ac_cv_have_gst_mpeg2_parser="yes"],
        [ac_cv_have_gst_mpeg2_parser="no"]
    )
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
])
AM_CONDITIONAL([USE_LOCAL_CODEC_PARSERS_MPEG2],
    [test "$ac_cv_have_gst_mpeg2_parser" != "yes"])

dnl ... H.264 parser, with the required extensions
AC_CACHE_CHECK([for H.264 parser],
    ac_cv_have_gst_h264_parser, [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $GST_CFLAGS $GST_CODEC_PARSERS_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $GST_LIBS $GST_CODEC_PARSERS_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <gst/codecparsers/gsth264parser.h>]],
            [[GstH264SliceHdr slice_hdr;
              GstH264VUIParams vui_params;
              GstH264Profile profile = GST_H264_PROFILE_HIGH;
              slice_hdr.n_emulation_prevention_bytes = 0;
              vui_params.par_n = 0;
              vui_params.par_d = 0;]])],
        [ac_cv_have_gst_h264_parser="yes"],
        [ac_cv_have_gst_h264_parser="no"]
    )
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
])
AM_CONDITIONAL([USE_LOCAL_CODEC_PARSERS_H264],
    [test "$ac_cv_have_gst_h264_parser" != "yes"])

dnl ... JPEG parser, not upstream yet
AC_CACHE_CHECK([for JPEG parser],
    ac_cv_have_gst_jpeg_parser, [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $GST_CFLAGS $GST_CODEC_PARSERS_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $GST_LIBS $GST_CODEC_PARSERS_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <gst/codecparsers/gstjpegparser.h>]],
            [[GstJpegImage jpeg_image;]])],
        [ac_cv_have_gst_jpeg_parser="yes"],
        [ac_cv_have_gst_jpeg_parser="no"]
    )
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
])
AM_CONDITIONAL([USE_LOCAL_CODEC_PARSERS_JPEG],
    [test "$ac_cv_have_gst_jpeg_parser" != "yes"])

case $GST_API_VERSION in
0.10)   lt_bias=gst0_vaapi_lt_current_bias;;
1.0)    lt_bias=gst1_vaapi_lt_current_bias;;
esac
GST_VAAPI_MAJOR_VERSION=`expr gst_vaapi_lt_current - "$lt_bias"`
AC_SUBST(GST_VAAPI_MAJOR_VERSION)

dnl GST_VAAPI_LT_LDFLAGS:
GST_VAAPI_LT_CURRENT="$GST_VAAPI_MAJOR_VERSION"
GST_VAAPI_LT_REV=gst_vaapi_lt_revision
GST_VAAPI_LT_AGE=gst_vaapi_lt_age
GST_VAAPI_LT_VERSION="$GST_VAAPI_LT_CURRENT:$GST_VAAPI_LT_REV:$GST_VAAPI_LT_AGE"
GST_VAAPI_LT_LDFLAGS="-version-info $GST_VAAPI_LT_VERSION"
AC_SUBST(GST_VAAPI_LT_VERSION)
AC_SUBST(GST_VAAPI_LT_LDFLAGS)

dnl GST_ALL_LDFLAGS:
dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_ALL_LDFLAGS="-no-undefined"
AC_SUBST(GST_ALL_LDFLAGS)

dnl GST_PLUGIN_LDFLAGS:
dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^([_]*gst_plugin_desc|gst_.*_get_type)\$\$' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl Check for the GStreamer plugins directory
AC_ARG_VAR([GST_PLUGIN_PATH], [installation path for gstreamer-vaapi plugin elements])
AC_MSG_CHECKING([for GStreamer plugins directory])
if test -d "$GST_PLUGIN_PATH"; then
    GST_PLUGINS_DIR="$GST_PLUGIN_PATH"
else
    GST_PLUGINS_DIR=`$PKG_CONFIG gstreamer-$GST_API_VERSION --variable pluginsdir`
    if test -z "$GST_PLUGINS_DIR"; then
        GST_PLUGINS_DIR="\$(libdir)/gstreamer-$GST_API_VERSION"
    fi
fi
AC_MSG_RESULT([$GST_PLUGINS_DIR])
plugindir="$GST_PLUGINS_DIR"
AC_SUBST(plugindir)

dnl ---------------------------------------------------------------------------
dnl -- Renderers                                                             --
dnl ---------------------------------------------------------------------------

dnl Check for DRM/libudev
USE_DRM=0
if test "$enable_drm" = "yes"; then
    PKG_CHECK_MODULES(DRM, [libdrm], [USE_DRM=1], [USE_DRM=0])
    PKG_CHECK_MODULES(UDEV, [libudev], [:], [USE_DRM=0])

    if test $USE_DRM -eq 1; then
        saved_CPPFLAGS="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS $DRM_CFLAGS"
        AC_CHECK_HEADERS([drm_fourcc.h], [:], [USE_DRM=0])
        CPPFLAGS="$saved_CPPFLAGS"
    fi
fi

dnl Check for X11
USE_X11=0
if test "$enable_x11" = "yes"; then
    PKG_CHECK_MODULES(X11, [x11], [USE_X11=1], [USE_X11=0])
    if test $USE_X11 -eq 1; then
        saved_CPPFLAGS="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS $X11_CFLAGS"
        AC_CHECK_HEADERS([X11/Xlib.h X11/Xutil.h X11/Xatom.h], [:], [USE_X11=0])
        CPPFLAGS="$saved_CPPFLAGS"
    fi
fi

dnl Check for XRandR
HAVE_XRANDR=0
if test $USE_X11 -eq 1; then
    HAVE_XRANDR=1
    PKG_CHECK_MODULES([XRANDR], [xrandr], [:], [HAVE_XRANDR=0])
    if test $HAVE_XRANDR -eq 1; then
        saved_CPPFLAGS="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS $XRANDR_CFLAGS"
        AC_CHECK_HEADERS([X11/extensions/Xrandr.h], [:], [HAVE_XRANDR=0])
        CPPFLAGS="$saved_CPPFLAGS"
    fi
fi
if test $HAVE_XRANDR -eq 1; then
    AC_DEFINE_UNQUOTED(HAVE_XRANDR, 1,
        [Defined to 1 if the XRandR extension exists.])
fi

dnl OpenGL
enable_opengl="no"
if test "$enable_glx" = "yes"; then
    enable_opengl="yes"
fi

HAVE_GL=0
if test "$enable_opengl" = "yes"; then
    HAVE_GL=1
    PKG_CHECK_MODULES([GL], [gl], [:], [HAVE_GL=0])
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $GL_CFLAGS"
    AC_CHECK_HEADERS([GL/gl.h GL/glext.h], [:], [HAVE_GL=0], [
#ifdef HAVE_GL_GL_H
# include <GL/gl.h>
#endif
    ])
    CPPFLAGS="$saved_CPPFLAGS"
fi

dnl ... GLX
USE_GLX=0
if test "$enable_glx" = "yes" -a $HAVE_GL -eq 1 -a $USE_X11 -eq 1; then
    USE_GLX=1
    saved_CPPFLAGS="$CPPFLAGS"
    saved_LIBS="$LIBS"
    CPPFLAGS="$CPPFLAGS $GL_CFLAGS"
    LIBS="$LIBS $GL_LIBS"
    AC_CHECK_HEADERS([GL/glx.h], [:], [USE_GLX=0], [
#ifdef HAVE_GL_GL_H
# include <GL/gl.h>
#endif
    ])
    AC_CHECK_LIB([GL], [glXCreateContext], [:], [USE_GLX=0])
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
fi

dnl Check for Wayland
USE_WAYLAND=0
if test "$enable_wayland" = "yes"; then
    PKG_CHECK_MODULES(WAYLAND, [wayland-client >= wayland_api_version],
        [USE_WAYLAND=1], [USE_WAYLAND=0])

    if test $USE_WAYLAND -eq 1; then
        saved_CPPFLAGS="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS $WAYLAND_CFLAGS"
        AC_CHECK_HEADERS([wayland-client.h], [:], [USE_WAYLAND=0])
        CPPFLAGS="$saved_CPPFLAGS"
    fi
fi

dnl ---------------------------------------------------------------------------
dnl -- VA-API                                                                --
dnl ---------------------------------------------------------------------------

dnl Core API
LIBVA_PKGNAME="libva"
PKG_CHECK_MODULES(LIBVA, [$LIBVA_PKGNAME >= va_api_version])
AC_SUBST(LIBVA_PKGNAME)

VA_VERSION=`$PKG_CONFIG --modversion libva`
VA_MAJOR_VERSION=`echo "$VA_VERSION" | cut -d'.' -f1`
VA_MINOR_VERSION=`echo "$VA_VERSION" | cut -d'.' -f2`
VA_MICRO_VERSION=`echo "$VA_VERSION" | cut -d'.' -f3`
VA_VERSION_STR="$VA_VERSION"

dnl VA/DRM API
HAVE_VA_DRM=0
if test $USE_DRM -eq 1; then
    PKG_CHECK_MODULES([LIBVA_DRM], [libva-drm >= va_api_drm_version],
        [HAVE_VA_DRM=1], [USE_DRM=0])

    if test $HAVE_VA_DRM -eq 1; then
        saved_CPPFLAGS="$CPPFLAGS"
        CPPFLAGS="$LIBVA_DRM_CFLAGS"
        AC_CHECK_HEADERS([va/va_drm.h], [:], [HAVE_VA_DRM=0 USE_DRM=0])
        CPPFLAGS="$saved_CPPFLAGS"
    fi
fi

dnl VA/X11 API
HAVE_VA_X11=0
LIBVA_X11_PKGNAME="libva-x11"
if test $USE_X11 -eq 1; then
    PKG_CHECK_MODULES(LIBVA_X11, [$LIBVA_X11_PKGNAME >= va_api_x11_version],
        [HAVE_VA_X11=1], [USE_X11=0])
fi
AC_SUBST(LIBVA_X11_PKGNAME)

dnl VA/GLX API
HAVE_VA_GLX=0
LIBVA_GLX_PKGNAME="libva-glx"
if test $USE_GLX -eq 1; then
    PKG_CHECK_MODULES(LIBVA_GLX, [$LIBVA_GLX_PKGNAME >= va_api_glx_version],
        [HAVE_VA_GLX=1], [LIBVA_GLX_PKGNAME="$LIBVA_X11_PKGNAME"])

    if test $HAVE_VA_GLX -eq 1; then
        saved_CPPFLAGS="$CPPFLAGS"
        CPPFLAGS="$LIBVA_GLX_CFLAGS"
        AC_CHECK_HEADERS([va/va_glx.h], [:], [HAVE_VA_GLX=0])
        CPPFLAGS="$saved_CPPFLAGS"
    fi
fi
AC_SUBST(LIBVA_GLX_PKGNAME)

dnl Check for JPEG decoding API (0.32.1+)
USE_JPEG_DECODER=0
AC_CACHE_CHECK([for JPEG decoding API],
    ac_cv_have_jpeg_decoding_api, [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $LIBVA_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $LIBVA_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <va/va.h>]],
            [[VAPictureParameterBufferJPEGBaseline pic_param;
              VASliceParameterBufferJPEGBaseline slice_param;
              VAHuffmanTableBufferJPEGBaseline huffman_table;
              VAIQMatrixBufferJPEGBaseline iq_matrix;]])],
        [ac_cv_have_jpeg_decoding_api="yes" USE_JPEG_DECODER=1],
        [ac_cv_have_jpeg_decoding_api="no"]
    )
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
])

dnl VA/Wayland API
if test "$enable_wayland" = "yes"; then
    PKG_CHECK_MODULES([LIBVA_WAYLAND], [libva-wayland >= va_api_wld_version],
        [:], [USE_WAYLAND=0])
fi

dnl ---------------------------------------------------------------------------
dnl -- Generate files and summary                                            --
dnl ---------------------------------------------------------------------------

case ":$USE_X11:$USE_GLX:$USE_WAYLAND:$USE_DRM:" in
*:1:*)
    ;;
*)
    AC_MSG_ERROR([No renderer is enabled])
    ;;
esac

AC_DEFINE_UNQUOTED(USE_JPEG_DECODER, $USE_JPEG_DECODER,
    [Defined to 1 if JPEG decoder is used])
AM_CONDITIONAL(USE_JPEG_DECODER, test $USE_JPEG_DECODER -eq 1)

AC_DEFINE_UNQUOTED(USE_DRM, $USE_DRM,
    [Defined to 1 if DRM is enabled])
AM_CONDITIONAL(USE_DRM, test $USE_DRM -eq 1)

AC_DEFINE_UNQUOTED(USE_X11, $USE_X11,
    [Defined to 1 if X11 is enabled])
AM_CONDITIONAL(USE_X11, test $USE_X11 -eq 1)

AC_DEFINE_UNQUOTED(USE_GLX, $USE_GLX,
    [Defined to 1 if GLX is enabled])
AM_CONDITIONAL(USE_GLX, test $USE_GLX -eq 1)

AC_DEFINE_UNQUOTED(USE_WAYLAND, $USE_WAYLAND,
    [Defined to 1 if WAYLAND is enabled])
AM_CONDITIONAL(USE_WAYLAND, test $USE_WAYLAND -eq 1)

pkgconfigdir=${libdir}/pkgconfig
AC_SUBST(pkgconfigdir)

AC_CONFIG_FILES([
    Makefile
    debian.upstream/Makefile
    debian.upstream/changelog
    debian.upstream/control
    debian.upstream/gstreamer$GST_API_VERSION-vaapi-doc.install:\
debian.upstream/gstreamer-vaapi-doc.install.in
    debian.upstream/gstreamer$GST_API_VERSION-vaapi.install:\
debian.upstream/gstreamer-vaapi.install.in
    debian.upstream/libgstvaapi$GST_VAAPI_MAJOR_VERSION.install:\
debian.upstream/libgstvaapi.install.in
    debian.upstream/libgstvaapi-dev.install
    debian.upstream/libgstvaapi-drm-$GST_VAAPI_MAJOR_VERSION.install:\
debian.upstream/libgstvaapi-drm.install.in
    debian.upstream/libgstvaapi-glx-$GST_VAAPI_MAJOR_VERSION.install:\
debian.upstream/libgstvaapi-glx.install.in
    debian.upstream/libgstvaapi-wayland-$GST_VAAPI_MAJOR_VERSION.install:\
debian.upstream/libgstvaapi-wayland.install.in
    debian.upstream/libgstvaapi-x11-$GST_VAAPI_MAJOR_VERSION.install:\
debian.upstream/libgstvaapi-x11.install.in
    docs/Makefile
    docs/reference/Makefile
    docs/reference/libs/Makefile
    docs/reference/libs/libs-docs.xml
    docs/reference/plugins/Makefile
    docs/reference/plugins/plugins-docs.xml
    ext/Makefile
    gst-libs/Makefile
    gst-libs/gst/Makefile
    gst-libs/gst/codecparsers/Makefile
    gst-libs/gst/vaapi/Makefile
    gst-libs/gst/video/Makefile
    gst/Makefile
    gst/vaapi/Makefile
    pkgconfig/Makefile
    pkgconfig/gstreamer-vaapi-$GST_API_VERSION.pc:\
pkgconfig/gstreamer-vaapi.pc.in
    pkgconfig/gstreamer-vaapi-drm-$GST_API_VERSION.pc:\
pkgconfig/gstreamer-vaapi-drm.pc.in
    pkgconfig/gstreamer-vaapi-glx-$GST_API_VERSION.pc:\
pkgconfig/gstreamer-vaapi-glx.pc.in
    pkgconfig/gstreamer-vaapi-wayland-$GST_API_VERSION.pc:\
pkgconfig/gstreamer-vaapi-wayland.pc.in
    pkgconfig/gstreamer-vaapi-x11-$GST_API_VERSION.pc:\
pkgconfig/gstreamer-vaapi-x11.pc.in
    tests/Makefile
])
AC_OUTPUT

dnl Print summary
yesno() {
    test $1 -eq 1 && echo yes || echo no
}

VIDEO_OUTPUTS=""
AS_IF([test $USE_DRM -eq 1], [VIDEO_OUTPUTS="$VIDEO_OUTPUTS drm"])
AS_IF([test $USE_X11 -eq 1], [VIDEO_OUTPUTS="$VIDEO_OUTPUTS x11"])
AS_IF([test $USE_GLX -eq 1], [VIDEO_OUTPUTS="$VIDEO_OUTPUTS glx"])
AS_IF([test $USE_WAYLAND -eq 1], [VIDEO_OUTPUTS="$VIDEO_OUTPUTS wayland"])

echo
echo $PACKAGE configuration summary:
echo
echo GStreamer API version ............ : $GST_API_VERSION
echo VA-API version ................... : $VA_VERSION_STR
echo Video outputs .................... : $VIDEO_OUTPUTS
echo
