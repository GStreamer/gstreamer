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
m4_define([va_api_x11_version], [0.31.0])
m4_define([va_api_glx_version], [0.32.0])

# libva package version number
m4_define([libva_x11_package_version], [1.0.3])
m4_define([libva_glx_package_version], [1.0.9])

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
AC_PROG_LIBTOOL

AC_ARG_ENABLE(glx,
    AS_HELP_STRING([--enable-glx],
                   [enable OpenGL/X11 @<:@default=yes@:>@]),
    [], [enable_glx="yes"])

AC_ARG_ENABLE(vaapi-glx,
    AS_HELP_STRING([--enable-vaapi-glx],
                   [enable VA/GLX extensions @<:@default=yes@:>@]),
    [], [enable_vaapi_glx="yes"])

AC_ARG_ENABLE(vaapisink-glx,
    AS_HELP_STRING([--enable-vaapisink-glx],
                   [enable OpenGL/X11 to vaapisink @<:@default=yes@:>@]),
    [], [enable_vaapisink_glx="no"])

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

dnl ... GST_PLUGINS_BASE_CHECK_VERSION() implementation
V=`$PKG_CONFIG --modversion gstreamer-plugins-base-$GST_MAJORMINOR`
GST_PLUGINS_BASE_MAJOR_VERSION=`echo "$V" | cut -d'.' -f1`
GST_PLUGINS_BASE_MINOR_VERSION=`echo "$V" | cut -d'.' -f2`
GST_PLUGINS_BASE_MICRO_VERSION=`echo "$V" | cut -d'.' -f3`
AC_SUBST(GST_PLUGINS_BASE_MAJOR_VERSION)
AC_SUBST(GST_PLUGINS_BASE_MINOR_VERSION)
AC_SUBST(GST_PLUGINS_BASE_MICRO_VERSION)

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

dnl Check for X11
PKG_CHECK_MODULES(X11, [x11])

dnl Check for OpenGL
USE_GLX=1
if test "$enable_glx" != "yes"; then
    USE_GLX=0
fi
GLX_CFLAGS=""
GLX_LIBS=""
AC_CHECK_HEADERS([GL/gl.h GL/glext.h GL/glx.h], [], [USE_GLX=0], [
#ifdef HAVE_GL_GL_H
# include <GL/gl.h>
#endif
])
AC_CHECK_LIB(GL, glXCreateContext, [GLX_LIBS="-lGL"], [USE_GLX=0])
AC_SUBST(GLX_CFLAGS)
AC_SUBST(GLX_LIBS)

dnl ---------------------------------------------------------------------------
dnl -- VA-API                                                                --
dnl ---------------------------------------------------------------------------

dnl Check for VA-API
LIBVA_PKGNAME="libva"
PKG_CHECK_MODULES(LIBVA, [$LIBVA_PKGNAME])
AC_SUBST(LIBVA_PKGNAME)

dnl ... original VA-API 0.29
AC_CACHE_CHECK([for old VA-API 0.29],
    ac_cv_have_vaapi_old, [
    saved_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $LIBVA_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $LIBVA_LIBS"
    AC_LINK_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <va_x11.h>]],
            [[vaGetDisplay(NULL)]])],
        [ac_cv_have_vaapi_old="yes"],
        [ac_cv_have_vaapi_old="no"]
    )
    CFLAGS="$saved_CFLAGS"
    LIBS="$saved_LIBS"
])
if test "$ac_cv_have_vaapi_old" = "yes"; then
    LIBVA_EXTRA_CFLAGS="$LIBVA_CFLAGS -DGST_VAAPI_USE_OLD_VAAPI_0_29"
fi

dnl ... VA-API >= 0.31 or -sds
LIBVA_X11_PKGNAME="libva-x11"
PKG_CHECK_MODULES(LIBVA_X11, [$LIBVA_X11_PKGNAME],
    [ac_cv_have_vaapi_x11="yes"],
    [ac_cv_have_vaapi_x11="no"]
)
if test "$ac_cv_have_vaapi_x11" = "no"; then
    if test "$ac_cv_have_vaapi_old" = "yes"; then
        LIBVA_X11_PKGNAME="libva"
        CFLAGS="$CFLAGS $LIBVA_EXTRA_CFLAGS"
        LIBS="$LIBS $LIBVA_EXTRA_LIBS"
    else
        AC_MSG_ERROR([could not find VA-API])
    fi
fi
AC_SUBST(LIBVA_X11_PKGNAME)

dnl ... VA-API >= 0.31 or -sds (VA/GLX extensions)
USE_VAAPI_GLX=0
if test $USE_GLX -eq 1; then
    if test "$enable_vaapi_glx" = "yes"; then
        LIBVA_GLX_PKGNAME="libva-glx"
        PKG_CHECK_MODULES(LIBVA_GLX, [$LIBVA_GLX_PKGNAME],
            [ac_cv_have_vaapi_glx="yes" USE_VAAPI_GLX=1],
            [ac_cv_have_vaapi_glx="no"]
        )
    fi
    if test $USE_VAAPI_GLX -eq 0; then
        AC_MSG_WARN([VA/GLX not found or disabled. Fallbacking to TFP+FBO])
        LIBVA_GLX_PKGNAME="$LIBVA_X11_PKGNAME"
    fi
fi
AC_SUBST(LIBVA_GLX_PKGNAME)

AC_SUBST(LIBVA_EXTRA_CFLAGS)
AC_SUBST(LIBVA_EXTRA_LIBS)

dnl Check for JPEG decoding API (0.33+)
USE_JPEG_DECODER=0
AC_CACHE_CHECK([for JPEG decoding API],
    ac_cv_have_jpeg_decoding_api, [
    saved_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $LIBVA_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$CFLAGS $LIBVA_LIBS"
    AC_COMPILE_IFELSE(
        [AC_LANG_PROGRAM(
            [[#include <va/va.h>
              #include <va/va_dec_jpeg.h>]],
            [[VAPictureParameterBufferJPEG pic_param;
              VASliceParameterBufferJPEG slice_param;
              VAHuffmanTableBufferJPEG huffman_table;
              VAIQMatrixBufferJPEG iq_matrix;]])],
        [ac_cv_have_jpeg_decoding_api="yes" USE_JPEG_DECODER=1],
        [ac_cv_have_jpeg_decoding_api="no"]
    )
    CFLAGS="$saved_CFLAGS"
    LIBS="$saved_LIBS"
])

dnl Check for OpenGL support to vaapisink
if test "$enable_vaapisink_glx:$USE_GLX" = "yes:1"; then
    USE_VAAPISINK_GLX=1
else
    USE_VAAPISINK_GLX=0
fi

dnl ---------------------------------------------------------------------------
dnl -- Generate files and summary                                            --
dnl ---------------------------------------------------------------------------

AC_DEFINE_UNQUOTED(USE_JPEG_DECODER, $USE_JPEG_DECODER,
    [Defined to 1 if JPEG decoder is used])
AM_CONDITIONAL(USE_JPEG_DECODER, test $USE_JPEG_DECODER -eq 1)

AC_DEFINE_UNQUOTED(USE_GLX, $USE_GLX,
    [Defined to 1 if GLX is enabled])
AM_CONDITIONAL(USE_GLX, test $USE_GLX -eq 1)

AC_DEFINE_UNQUOTED(USE_VAAPI_GLX, $USE_VAAPI_GLX,
    [Defined to 1 if VA/GLX is enabled])
AM_CONDITIONAL(USE_VAAPI_GLX, test $USE_VAAPI_GLX -eq 1)

AC_DEFINE_UNQUOTED(USE_VAAPISINK_GLX, $USE_VAAPISINK_GLX,
    [Defined to 1 to enable GLX support to vaapisink])
AM_CONDITIONAL(USE_VAAPISINK_GLX, test $USE_VAAPISINK_GLX -eq 1)

VA_VERSION=`$PKG_CONFIG --modversion libva`
VA_MAJOR_VERSION=`echo "$VA_VERSION" | cut -d'.' -f1`
VA_MINOR_VERSION=`echo "$VA_VERSION" | cut -d'.' -f2`
VA_MICRO_VERSION=`echo "$VA_VERSION" | cut -d'.' -f3`
VA_VERSION_STR="$VA_VERSION"

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
    debian.upstream/libgstvaapi-glx-$GST_VAAPI_MAJOR_VERSION.install:\
debian.upstream/libgstvaapi-glx.install.in
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
    gst-libs/gst/gstutils_version.h
    gst-libs/gst/vaapi/Makefile
    gst/Makefile
    gst/vaapi/Makefile
    pkgconfig/Makefile
    pkgconfig/gstreamer-vaapi-$GST_MAJORMINOR.pc:\
pkgconfig/gstreamer-vaapi.pc.in
    pkgconfig/gstreamer-vaapi-glx-$GST_MAJORMINOR.pc:\
pkgconfig/gstreamer-vaapi-glx.pc.in
    pkgconfig/gstreamer-vaapi-x11-$GST_MAJORMINOR.pc:\
pkgconfig/gstreamer-vaapi-x11.pc.in
    tests/Makefile
])
AC_OUTPUT

dnl Print summary
yesno() {
    test $1 -eq 1 && echo yes || echo no
}

echo
echo $PACKAGE configuration summary:
echo
echo VA-API version ................... : $VA_VERSION_STR
echo GLX support ...................... : $(yesno $USE_GLX)
echo VA/GLX support ................... : $(yesno $USE_VAAPI_GLX)
echo VaapiSink/GL ..................... : $(yesno $USE_VAAPISINK_GLX)
echo
