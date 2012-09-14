# gstreamer-vaapi package version number
m4_define([gst_vaapi_major_version], [0])
m4_define([gst_vaapi_minor_version], [4])
m4_define([gst_vaapi_micro_version], [0])
m4_define([gst_vaapi_pre_version],   [1])
m4_define([gst_vaapi_version],
          [gst_vaapi_major_version.gst_vaapi_minor_version.gst_vaapi_micro_version])
m4_if(gst_vaapi_pre_version, [0], [], [
m4_append([gst_vaapi_version], gst_vaapi_pre_version, [.pre])
])

# glib version number
m4_define([glib_major_version], [2])
m4_define([glib_minor_version], [28])
m4_define([glib_micro_version], [0])
m4_define([glib_major_minor_version],
          [glib_major_version.glib_minor_version])
m4_define([glib_version],
          [glib_major_version.glib_minor_version.glib_micro_version])

# gst version number
m4_define([gst_major_version], [0])
m4_define([gst_minor_version], [10])
m4_define([gst_micro_version], [14])
m4_define([gst_major_minor_version],
          [gst_major_version.gst_minor_version])
m4_define([gst_version],
          [gst_major_version.gst_minor_version.gst_micro_version])

# gst plugins-base version number
m4_define([gst_plugins_base_major_version], [0])
m4_define([gst_plugins_base_minor_version], [10])
m4_define([gst_plugins_base_micro_version], [31])
m4_define([gst_plugins_base_version],
          [gst_plugins_base_major_version.gst_plugins_base_minor_version.gst_plugins_base_micro_version])

# gst plugins-bad version number
m4_define([gst_plugins_bad_major_version], [0])
m4_define([gst_plugins_bad_minor_version], [10])
m4_define([gst_plugins_bad_micro_version], [22])
m4_define([gst_plugins_bad_version],
          [gst_plugins_bad_major_version.gst_plugins_bad_minor_version.gst_plugins_bad_micro_version])

# VA-API minimum version number
m4_define([va_api_version],     [0.30.4])
m4_define([va_api_drm_version], [0.34.0])
m4_define([va_api_x11_version], [0.31.0])
m4_define([va_api_glx_version], [0.32.0])
m4_define([va_api_wld_version], [0.34.0])

# libva package version number
m4_define([libva_drm_package_version], [1.2.0])
m4_define([libva_x11_package_version], [1.0.3])
m4_define([libva_glx_package_version], [1.0.9])
m4_define([libva_wld_package_version], [1.2.0])

# gtk-doc version number
# XXX: introspection annotations require gtk-doc >= 1.12
m4_define([gtkdoc_major_version], [1])
m4_define([gtkdoc_minor_version], [9])
m4_define([gtkdoc_version], [gtkdoc_major_version.gtkdoc_minor_version])

AC_PREREQ([2.58])
AC_INIT([gst_vaapi], [gst_vaapi_version],
    [gwenole.beauchesne@intel.com],
    [gstreamer-vaapi])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE

TODAY="`LC_ALL=C date +'%a, %d %b %Y %X %z'`"
AC_SUBST(TODAY)

GST_VAAPI_MAJOR_VERSION=gst_vaapi_major_version
AC_SUBST(GST_VAAPI_MAJOR_VERSION)

LIBVA_PACKAGE_VERSION=libva_x11_package_version
AC_SUBST(LIBVA_PACKAGE_VERSION)

dnl Versions for GStreamer and plugins-base
GST_MAJORMINOR=gst_major_minor_version
GST_VERSION_REQUIRED=gst_version
GST_PLUGINS_BASE_VERSION_REQUIRED=gst_plugins_base_version
GST_PLUGINS_BAD_VERSION_REQUIRED=gst_plugins_bad_version
AC_SUBST(GST_MAJORMINOR)
AC_SUBST(GST_VERSION_REQUIRED)
AC_SUBST(GST_PLUGINS_BASE_VERSION_REQUIRED)
AC_SUBST(GST_PLUGINS_BAD_VERSION_REQUIRED)

dnl Use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])], [
  AM_DEFAULT_VERBOSITY=1
  AC_SUBST(AM_DEFAULT_VERBOSITY)
])

dnl Check for tools
AC_PROG_CC
AM_PROG_CC_C_O

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

dnl ---------------------------------------------------------------------------
dnl -- GStreamer                                                             --
dnl ---------------------------------------------------------------------------

dnl GStreamer Core
PKG_CHECK_MODULES([GST],      [gstreamer-$GST_MAJORMINOR >= gst_version])
PKG_CHECK_MODULES([GST_BASE], [gstreamer-base-$GST_MAJORMINOR >= gst_version])

AC_CACHE_CHECK([for GstBaseSink::query hook], ac_cv_have_gst_base_sink_query, [
    saved_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $GST_BASE_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $GST_BASE_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <gst/base/gstbasesink.h>]],
            [[GstBaseSinkClass klass; klass.query = NULL;]])],
        [ac_cv_have_gst_base_sink_query="yes"],
        [ac_cv_have_gst_base_sink_query="no"]
    )
    CFLAGS="$saved_CFLAGS"
    LIBS="$saved_LIBS"
])
if test "$ac_cv_have_gst_base_sink_query" != "yes"; then
    AC_MSG_ERROR([GstBaseSink does not contain the 'query' vfunc])
fi

dnl GStreamer -base plugins
PKG_CHECK_MODULES([GST_PLUGINS_BASE],
    [gstreamer-plugins-base-$GST_MAJORMINOR >= gst_plugins_base_version])
PKG_CHECK_MODULES([GST_INTERFACES],
    [gstreamer-interfaces-$GST_MAJORMINOR >= gst_plugins_base_version])

dnl ... GstVideoOverlayComposition (gstreamer-video)
PKG_CHECK_MODULES([GST_VIDEO],
    [gstreamer-video-$GST_MAJORMINOR >= gst_plugins_base_version])

AC_CACHE_CHECK([for GstVideoOverlayComposition],
    ac_cv_have_gst_video_overlay_composition, [
    saved_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $GST_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $GST_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <gst/video/video-overlay-composition.h>]],
            [[GstVideoOverlayComposition *c = gst_video_overlay_composition_new(0);]])],
        [ac_cv_have_gst_video_overlay_composition="yes"],
        [ac_cv_have_gst_video_overlay_composition="no"]
    )
    CFLAGS="$saved_CFLAGS"
    LIBS="$saved_LIBS"
])
if test "$ac_cv_have_gst_video_overlay_composition" != "yes"; then
    AC_MSG_ERROR([GstVideoOverlayComposition is not available])
fi

dnl GStreamer -bad plugins
PKG_CHECK_MODULES([GST_BASEVIDEO],
    [gstreamer-basevideo-$GST_MAJORMINOR >= gst_plugins_bad_version])

dnl ... bitstream parsers
USE_CODEC_PARSERS=1
USE_LOCAL_CODEC_PARSERS=0

PKG_CHECK_MODULES([GST_CODEC_PARSERS],
    [gstreamer-codecparsers-$GST_MAJORMINOR >= gst_plugins_bad_version])

dnl ... 0.10.23 addition, could be implemented otherwise
AC_CACHE_CHECK([for GstH264SliceHdr::n_emulation_prevention_bytes],
    ac_cv_have_gst_h264_slice_hdr_epb_count, [
    saved_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $GST_CFLAGS $GST_CODEC_PARSERS_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $GST_LIBS $GST_CODEC_PARSERS_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <gst/codecparsers/gsth264parser.h>]],
            [[GstH264SliceHdr slice_hdr;
              slice_hdr.n_emulation_prevention_bytes = 0;]])],
        [ac_cv_have_gst_h264_slice_hdr_epb_count="yes"],
        [ac_cv_have_gst_h264_slice_hdr_epb_count="no"]
    )
    CFLAGS="$saved_CFLAGS"
    LIBS="$saved_LIBS"
])

if test "$ac_cv_have_gst_h264_slice_hdr_epb_count" = "yes"; then
    AC_DEFINE_UNQUOTED(HAVE_GST_H264_SLICE_HDR_EPB_COUNT, 1,
        [Defined to 1 if GstH264SliceHdr::n_emulation_prevention_bytes exists.])
fi

dnl ... JPEG parser, not upstream yet
AC_CACHE_CHECK([for JPEG parser],
    ac_cv_have_gst_jpeg_parser, [
    saved_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $GST_CFLAGS $GST_CODEC_PARSERS_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $GST_LIBS $GST_CODEC_PARSERS_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <gst/codecparsers/gstjpegparser.h>]],
            [[GstJpegImage jpeg_image;]])],
        [ac_cv_have_gst_jpeg_parser="yes"],
        [ac_cv_have_gst_jpeg_parser="no" USE_LOCAL_CODEC_PARSERS=1]
    )
    CFLAGS="$saved_CFLAGS"
    LIBS="$saved_LIBS"
])

if test "$ac_cv_have_gst_jpeg_parser" = "yes"; then
    AC_DEFINE_UNQUOTED(HAVE_GST_JPEG_PARSER, 1,
        [Defined to 1 if JPEG parser exists.])
fi
AM_CONDITIONAL(USE_LOCAL_CODEC_PARSERS, test $USE_LOCAL_CODEC_PARSERS -eq 1)

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
AC_MSG_CHECKING([for GStreamer plugins directory])
GST_PLUGINS_DIR=`$PKG_CONFIG gstreamer-$GST_MAJORMINOR --variable pluginsdir`
if test -z "$GST_PLUGINS_DIR"; then
    echo "FAIL FAIL FAIL"
    GST_PLUGINS_DIR="\$(libdir)/gstreamer-$GST_MAJORMINOR"
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
    PKG_CHECK_MODULES(WAYLAND, [wayland-client],
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
    saved_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $LIBVA_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$CFLAGS $LIBVA_LIBS"
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
    CFLAGS="$saved_CFLAGS"
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
    debian.upstream/gstreamer$GST_MAJORMINOR-vaapi-doc.install:\
debian.upstream/gstreamer-vaapi-doc.install.in
    debian.upstream/gstreamer$GST_MAJORMINOR-vaapi.install:\
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
    gst-libs/Makefile
    gst-libs/gst/Makefile
    gst-libs/gst/codecparsers/Makefile
    gst-libs/gst/vaapi/Makefile
    gst/Makefile
    gst/vaapi/Makefile
    pkgconfig/Makefile
    pkgconfig/gstreamer-vaapi-$GST_MAJORMINOR.pc:\
pkgconfig/gstreamer-vaapi.pc.in
    pkgconfig/gstreamer-vaapi-drm-$GST_MAJORMINOR.pc:\
pkgconfig/gstreamer-vaapi-drm.pc.in
    pkgconfig/gstreamer-vaapi-glx-$GST_MAJORMINOR.pc:\
pkgconfig/gstreamer-vaapi-glx.pc.in
    pkgconfig/gstreamer-vaapi-wayland-$GST_MAJORMINOR.pc:\
pkgconfig/gstreamer-vaapi-wayland.pc.in
    pkgconfig/gstreamer-vaapi-x11-$GST_MAJORMINOR.pc:\
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
echo VA-API version ................... : $VA_VERSION_STR
echo Video outputs .................... : $VIDEO_OUTPUTS
echo
