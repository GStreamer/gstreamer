# gstreamer-vaapi package version number
m4_define([gst_vaapi_major_version], [1])
m4_define([gst_vaapi_minor_version], [12])
m4_define([gst_vaapi_micro_version], [2])
m4_define([gst_vaapi_nano_version],  [0])
m4_define([gst_vaapi_version],
          [gst_vaapi_major_version.gst_vaapi_minor_version.gst_vaapi_micro_version])
m4_if(gst_vaapi_nano_version, [0], [],
  [m4_append([gst_vaapi_version], gst_vaapi_nano_version, [.])])

# Configure defaults
m4_define([default_glapi],              [any])

dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
# gstreamer-vaapi library (libtool) version number
m4_define([gst_vaapi_lt_current],       [1202])
m4_define([gst_vaapi_lt_revision],      [0])
m4_define([gst_vaapi_lt_age],           [1202])

# glib version number
m4_define([glib_version], [2.40])

# gstreamer version number
m4_define([gst_version],                [1.12.0])
m4_define([gst_plugins_base_version],   [1.12.0])
m4_define([gst_plugins_bad_version],    [1.12.0])

# Wayland minimum version number
m4_define([wayland_api_version], [1.0.2])

# VA-API minimum version number
m4_define([va_api_version],     [0.30.4])
m4_define([va_api_enc_version], [0.34.0])
m4_define([va_api_drm_version], [0.33.0])
m4_define([va_api_x11_version], [0.31.0])
m4_define([va_api_wld_version], [0.33.0])

# gtk-doc version number
# XXX: introspection annotations require gtk-doc >= 1.12
m4_define([gtkdoc_version], [1.12])

AC_PREREQ([2.69])
AC_INIT([GStreamer VA-API Plug-ins], [gst_vaapi_version],
  [http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer],
  [gstreamer-vaapi])

dnl define the output header for config
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability 1.14 no-dist-gzip dist-xz tar-ustar subdir-objects])

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_GIT="no", GST_GIT="yes")

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE([enable])

dnl use pretty build output by default
AM_SILENT_RULES([yes])

dnl our libraries and install dirs use GST_API_VERSION in the filename
dnl to allow side-by-side installation of different API versions
GST_API_VERSION=1.0
AC_SUBST([GST_API_VERSION])
AC_DEFINE_UNQUOTED([GST_API_VERSION], ["$GST_API_VERSION"],
  [GStreamer API Version])

AG_GST_LIBTOOL_PREPARE

dnl CURRENT, REVISION, AGE
dnl sets GST_LT_LDFLAGS
AS_LIBTOOL(GST, gst_vaapi_lt_current, gst_vaapi_lt_revision, gst_vaapi_lt_age)

dnl *** required versions of GStreamer stuff ***
GLIB_REQ=glib_version
GST_REQ=gst_version
GST_PBREQ=gst_plugins_base_version
GST_PBADREQ=gst_plugins_bad_version
WAYLAND_REQ=wayland_api_version
GTKDOC_REQ=gtkdoc_version

dnl *** required versions of VA-API stuff ***
VAAPI_REQ=va_api_version
VAAPI_ENC_REQ=va_api_enc_version
VAAPI_DRM_REQ=va_api_drm_version
VAAPI_X11_REQ=va_api_x11_version
VAAPI_WLD_REQ=va_api_wld_version

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST([ACLOCAL_AMFLAGS], ["-I m4 -I common/m4"])

dnl *** check for arguments to configure ***

AG_GST_ARG_DISABLE_FATAL_WARNINGS
AG_GST_ARG_ENABLE_EXTRA_CHECKS
AG_GST_ARG_DEBUG

AG_GST_ARG_EXAMPLES

AG_GST_ARG_WITH_PKG_CONFIG_PATH

AG_GST_SET_PACKAGE_RELEASE_DATETIME_WITH_NANO([$PACKAGE_VERSION_NANO],
  ["${srcdir}/gstreamer-vaapi.doap"],
  [$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR.$PACKAGE_VERSION_MICRO])

AC_ARG_ENABLE([encoders],
  AS_HELP_STRING([--enable-encoders],
    [enable video encoders @<:@default=yes@:>@]),
  [], [enable_encoders="yes"])

AC_ARG_ENABLE([drm],
  AS_HELP_STRING([--enable-drm],
    [enable DRM backend @<:@default=yes@:>@]),
  [], [enable_drm="yes"])

AC_ARG_ENABLE([x11],
  AS_HELP_STRING([--enable-x11],
    [enable X11 output @<:@default=yes@:>@]),
  [], [enable_x11="yes"])

AC_ARG_ENABLE([glx],
  AS_HELP_STRING([--enable-glx],
    [enable OpenGL/X11 output @<:@default=yes@:>@]),
  [], [enable_glx="yes"])

AC_ARG_ENABLE([wayland],
  AC_HELP_STRING([--enable-wayland],
    [enable Wayland output @<:@default=yes@:>@]),
  [], [enable_wayland="yes"])

AC_ARG_ENABLE([egl],
  AS_HELP_STRING([--enable-egl],
    [enable EGL output @<:@default=yes@:>@]),
  [], [enable_egl="yes"])

AC_ARG_WITH([glapi],
  AS_HELP_STRING([--with-glapi=APIs],
    [build with the specified OpenGL APIs @<:@default=default_glapi@:>@]),
  [GLAPI="$with_glapi"], [GLAPI=default_glapi])

dnl *** checks for platform ***

dnl * hardware/architecture *

dnl check CPU type
AG_GST_ARCH

dnl *** checks for programs ***

dnl find a compiler
AC_PROG_CC
AC_PROG_CC_STDC

dnl check if the compiler supports '-c' and '-o' options
AM_PROG_CC_C_O

dnl check for documentation tools
GTK_DOC_CHECK([$GTKDOC_REQ])
AG_GST_PLUGIN_DOCS([$GTKDOC_REQ])

dnl *** checks for libraries ***
dnl check for libm, for sin() etc.
AC_CHECK_LIB([m], [tan])

dnl Check to see if dlopen/dlsym is in default libraries (like
dnl Solaris, which has it in libc), or if libdl is needed to get it.
AC_CHECK_FUNC([dlopen], [], [
  AC_CHECK_LIB([dl], [dlopen], [DLOPEN_LIBS="-ldl"])])
AC_SUBST([DLOPEN_LIBS])

dnl define LIBDIR so we can inform people where we live
AS_AC_EXPAND([LIBDIR], [$libdir])
AC_DEFINE_UNQUOTED([LIBDIR], ["$LIBDIR"], [library dir])

dnl set location of plugin directory
AG_GST_SET_PLUGINDIR

dnl *** checks for header files ***

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***

dnl *** checks for headers ***

dnl *** checks for dependency libraries ***

dnl ---------------------------------------------------------------------------
dnl -- GStreamer                                                             --
dnl ---------------------------------------------------------------------------
dnl GLib is required
AG_GST_GLIB_CHECK([$GLIB_REQ])

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST([$GST_API_VERSION], [$GST_REQ], [yes])

dnl back compatibility
AC_MSG_CHECKING([for GStreamer API version])
gst_api_version=`$PKG_CONFIG --modversion "gstreamer-$GST_API_VERSION"`
gst_major_version=`echo "$gst_api_version" | cut -d'.' -f1`
gst_minor_version=`echo "$gst_api_version" | cut -d'.' -f2`
GST_VERSION="${gst_major_version}.${gst_minor_version}"
AC_MSG_RESULT([$GST_VERSION])
AC_DEFINE_UNQUOTED([GST_API_VERSION_S], ["$GST_VERSION"],
  [Defined to the string representation of GStreamer version])

AG_GST_CHECK_GST_BASE([$GST_API_VERSION], [$GST_REQ], [yes])
AG_GST_CHECK_GST_PLUGINS_BASE([$GST_API_VERSION], [$GST_PBREQ], [yes])

dnl gst_dmabuf_memory_get_fd (gstreamer-allocators)
AG_GST_CHECK_MODULES([GST_ALLOCATORS],
  [gstreamer-allocators-$GST_API_VERSION], [$GST_PBREQ], [yes])

dnl GstVideoOverlayComposition (gstreamer-video)
AG_GST_CHECK_MODULES([GST_VIDEO],
  [gstreamer-video-$GST_API_VERSION], [$GST_PBREQ], [yes])

dnl ... GStreamer base utils (gstreamer-pbutils)
AG_GST_CHECK_MODULES([GST_PBUTILS],
  [gstreamer-pbutils-$GST_API_VERSION], [$GST_PBREQ], [yes])

dnl bitstream parsers (gstreamer-codecparsers)
AG_GST_CHECK_MODULES([GST_CODEC_PARSERS],
  [gstreamer-codecparsers-$GST_API_VERSION], [$GST_PBADREQ], [yes])

AS_IF([test "x$enable_glx" = "xyes" -o "x$enable_egl" = "xyes"],
  [enable_opengl="yes"], [enable_opengl="no"])

dnl ... opengl helper libraries
HAVE_GSTGL=0
if test "x$enable_opengl" = "xyes"; then
  PKG_CHECK_MODULES([GST_GL],
    [gstreamer-gl-$GST_API_VERSION >= $GST_PBADREQ],
    [
      HAVE_GSTGL=1
      AC_CACHE_CHECK([for GStreamer OpenGL helper libraries],
        [ac_cv_have_gst_gl_helpers],
        [
          saved_CPPFLAGS="$CPPFLAGS"
          CPPFLAGS="$CPPFLAGS $GST_GL_CFLAGS $GST_VIDEO_CFLAGS"
          saved_LIBS="$LIBS"
          LIBS="$saved_LIBS"
          AC_CHECK_HEADERS([gst/gl/gl.h], [], [HAVE_GSTGL=0])
          AC_COMPILE_IFELSE(
            [
              AC_LANG_PROGRAM(
                [[
#include <gst/gl/gl.h>
                ]],
                [[
GstGLContext gl_context;
                ]])
            ],
            [ac_cv_have_gst_gl_helpers="yes"],
            [ac_cv_have_gst_gl_helpers="no"])
          CPPFLAGS="$saved_CPPFLAGS"
          LIBS="$saved_LIBS"
        ])
    ], [:])
fi
AS_IF([test "x$ac_cv_have_gst_gl_helpers" = "xno"], [HAVE_GSTGL=0])
AM_CONDITIONAL([USE_GST_GL_HELPERS], [test $HAVE_GSTGL -eq 1])
AC_DEFINE_UNQUOTED([USE_GST_GL_HELPERS], [$HAVE_GSTGL],
  [Defined to 1 if GStreamer OpenGL helper libraries are available])

if test "x$enable_egl" = "xyes" -a $HAVE_GSTGL -ne 1; then
  AC_MSG_WARN([GStreamer/GL helper libraries not found, disabling EGL support])
  enable_egl="no"
fi

dnl ---------------------------------------------------------------------------
dnl -- Renderers                                                             --
dnl ---------------------------------------------------------------------------

dnl Check for DRM/libudev
USE_DRM=0
if test "x$enable_drm" = "xyes"; then
  PKG_CHECK_MODULES([DRM], [libdrm libudev],
    [
      USE_DRM=1
      saved_CPPFLAGS="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $DRM_CFLAGS"
      AC_CHECK_HEADERS([drm_fourcc.h], [], [USE_DRM=0])
      CPPFLAGS="$saved_CPPFLAGS"
    ], [:])
fi

dnl Check for X11
USE_X11=0
if test "x$enable_x11" = "xyes"; then
  PKG_CHECK_MODULES([X11], [x11],
    [
      USE_X11=1
      saved_CPPFLAGS="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $X11_CFLAGS"
      AC_CHECK_HEADERS([X11/Xlib.h X11/Xutil.h X11/Xatom.h], [], [USE_X11=0])
      CPPFLAGS="$saved_CPPFLAGS"
    ], [:])
fi

HAVE_XKBLIB=0
HAVE_XRANDR=0
HAVE_XRENDER=0
if test $USE_X11 -eq 1; then
  dnl Check for XKB library
  HAVE_XKBLIB=1
  saved_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $X11_CFLAGS"
  AC_CHECK_HEADERS([X11/XKBlib.h], [], [HAVE_XKBLIB=0])
  CPPFLAGS="$saved_CPPFLAGS"

  dnl Check for XRandR
  PKG_CHECK_MODULES([XRANDR], [xrandr],
    [
      HAVE_XRANDR=1
      saved_CPPFLAGS="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $XRANDR_CFLAGS"
      AC_CHECK_HEADERS([X11/extensions/Xrandr.h], [], [HAVE_XRANDR=0])
      CPPFLAGS="$saved_CPPFLAGS"
    ], [:])

  dnl Check for XRender
  PKG_CHECK_MODULES([XRENDER], [xrender],
    [
      HAVE_XRENDER=1
      saved_CPPFLAGS="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $XRENDER_CFLAGS"
      AC_CHECK_HEADERS([X11/extensions/Xrender.h], [], [HAVE_XRENDER=0])
      CPPFLAGS="$saved_CPPFLAGS"
    ], [:])
fi

AC_DEFINE_UNQUOTED([HAVE_XKBLIB], [$HAVE_XKBLIB],
  [Defined to 1 if the XKB extension exists.])
AC_DEFINE_UNQUOTED([HAVE_XRANDR], [$HAVE_XRANDR],
  [Defined to 1 if the XRandR extension exists.])
AC_DEFINE_UNQUOTED([HAVE_XRENDER], [$HAVE_XRENDER],
  [Defined to 1 if the XRender extension exists.])

dnl OpenGL
GLES_VERSION_MASK=0

HAVE_GL=0
HAVE_GLESv2=0
HAVE_GLESv3=0
if test "x$enable_opengl" = "xyes"; then
  dnl OpenGL
  case ",$GLAPI," in
    (*,any,*|*,gl,*)
      HAVE_GL=1
      PKG_CHECK_MODULES([GL], [gl],
        [
          saved_CPPFLAGS="$CPPFLAGS"
          CPPFLAGS="$CPPFLAGS $GL_CFLAGS"
          AC_CHECK_HEADERS([GL/gl.h GL/glext.h], [], [HAVE_GL=0],
            [
#ifdef HAVE_GL_GL_H
# include <GL/gl.h>
#endif
            ])
          CPPFLAGS="$saved_CPPFLAGS"
        ], [HAVE_GL=0])
      GLES_VERSION_MASK=`expr $GLES_VERSION_MASK "+" $HAVE_GL "*" 1`
    ;;
  esac

  dnl OpenGL|ESv2
  case ",$GLAPI," in
    (*,any,*|*,gles2,*)
      HAVE_GLESv2=1
      PKG_CHECK_MODULES([GLES2], [glesv2],
        [
          saved_CPPFLAGS="$CPPFLAGS"
          CPPFLAGS="$CPPFLAGS $GLES2_CFLAGS"
          AC_CHECK_HEADERS([GLES2/gl2.h GLES2/gl2ext.h], [], [HAVE_GLESv2=0],
            [
#ifdef HAVE_GLES2_GL2_H
# include <GLES2/gl2.h>
#endif
            ])
          CPPFLAGS="$saved_CPPFLAGS"
        ], [HAVE_GLESv2=0])
      GLES_VERSION_MASK=`expr $GLES_VERSION_MASK "+" $HAVE_GLESv2 "*" 4`
    ;;
  esac

  dnl OpenGL|ESv3
  case ",$GLAPI," in
    (*,any,*|*,gles3,*)
      HAVE_GLESv3=1
      PKG_CHECK_MODULES([GLES3], [glesv2],
        [
          saved_CPPFLAGS="$CPPFLAGS"
          CPPFLAGS="$CPPFLAGS $GLES3_CFLAGS"
          AC_CHECK_HEADERS([GLES3/gl3.h GLES3/gl3ext.h GLES2/gl2ext.h], [],
            [HAVE_GLESv3=0],
            [
#ifdef HAVE_GLES3_GL3_H
# include <GLES3/gl3.h>
#endif
            ])
          CPPFLAGS="$saved_CPPFLAGS"
        ], [HAVE_GLESv3=0])
      GLES_VERSION_MASK=`expr $GLES_VERSION_MASK "+" $HAVE_GLESv3 "*" 8`
    ;;
  esac
fi

dnl ... GLX
USE_GLX=0
if test "x$enable_glx" = "xyes" -a $HAVE_GL -eq 1 -a $USE_X11 -eq 1; then
  USE_GLX=1

  saved_CPPFLAGS="$CPPFLAGS"
  saved_LIBS="$LIBS"
  CPPFLAGS="$CPPFLAGS $GL_CFLAGS"
  LIBS="$LIBS $GL_LIBS"
  AC_CHECK_HEADERS([GL/glx.h], [], [USE_GLX=0],
    [
#ifdef HAVE_GL_GL_H
# include <GL/gl.h>
#endif
    ])
  AC_CHECK_LIB([GL], [glXCreateContext], [], [USE_GLX=0])
  CPPFLAGS="$saved_CPPFLAGS"
  LIBS="$saved_LIBS"
fi

dnl ... EGL
USE_EGL=0
if test "x$enable_egl" = "xyes" -a $GLES_VERSION_MASK -ne 0; then
  PKG_CHECK_MODULES([EGL], [egl],
    [
      USE_EGL=1

      saved_CPPFLAGS="$CPPFLAGS"
      saved_LIBS="$LIBS"
      CPPFLAGS="$CPPFLAGS $EGL_CFLAGS"
      LIBS="$LIBS $EGL_LIBS"
      AC_CHECK_HEADERS([EGL/egl.h], [], [USE_EGL=0])
      AC_CHECK_LIB([EGL], [eglGetDisplay], [], [USE_EGL=0])
      CPPFLAGS="$saved_CPPFLAGS"
      LIBS="$saved_LIBS"
    ], [:])
fi

dnl Check for Wayland
USE_WAYLAND=0
if test "x$enable_wayland" = "xyes"; then
  PKG_CHECK_MODULES([WAYLAND], [wayland-client >= $WAYLAND_REQ],
    [
      USE_WAYLAND=1
      saved_CPPFLAGS="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $WAYLAND_CFLAGS"
      AC_CHECK_HEADERS([wayland-client.h], [], [USE_WAYLAND=0])
      CPPFLAGS="$saved_CPPFLAGS"
    ], [:])
fi

dnl ---------------------------------------------------------------------------
dnl -- VA-API                                                                --
dnl ---------------------------------------------------------------------------

dnl Core API
PKG_CHECK_MODULES([LIBVA], [libva >= $VAAPI_REQ libva < 0.99.0])
VA_VERSION_STR=`$PKG_CONFIG --modversion libva`
VA_DRIVERS_PATH=`$PKG_CONFIG --variable=driverdir libva`
AC_DEFINE_UNQUOTED([VA_DRIVERS_PATH], ["$VA_DRIVERS_PATH"],
  [VA drivers path])

dnl VA/DRM API
if test $USE_DRM -eq 1; then
  PKG_CHECK_MODULES([LIBVA_DRM], [libva-drm >= $VAAPI_DRM_REQ],
    [
      saved_CPPFLAGS="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $LIBVA_DRM_CFLAGS"
      AC_CHECK_HEADERS([va/va_drm.h], [], [USE_DRM=0])
      CPPFLAGS="$saved_CPPFLAGS"
    ], [USE_DRM=0])
fi

dnl VA/X11 API
if test $USE_X11 -eq 1; then
  PKG_CHECK_MODULES(LIBVA_X11, [libva-x11 >= $VAAPI_X11_REQ],
    [], [USE_X11=0])
fi

dnl Check for va_dec_jpeg.h header
saved_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $LIBVA_CFLAGS"
AC_CHECK_HEADERS([va/va_dec_jpeg.h], [], [],
  [
#include <va/va.h>
  ])
CPPFLAGS="$saved_CPPFLAGS"

dnl Check for JPEG decoding API (0.32.1+)
USE_JPEG_DECODER=0
AC_CACHE_CHECK([for JPEG decoding API],
  [ac_cv_have_jpeg_decoding_api],
  [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $LIBVA_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $LIBVA_LIBS"
    AC_COMPILE_IFELSE(
      [
        AC_LANG_PROGRAM(
          [[
#include <va/va.h>
#ifdef HAVE_VA_VA_DEC_JPEG_H
# include <va/va_dec_jpeg.h>
#endif
          ]],
          [[
VAPictureParameterBufferJPEGBaseline pic_param;
VASliceParameterBufferJPEGBaseline slice_param;
VAHuffmanTableBufferJPEGBaseline huffman_table;
VAIQMatrixBufferJPEGBaseline iq_matrix;
          ]])
      ],
      [ac_cv_have_jpeg_decoding_api="yes"],
      [ac_cv_have_jpeg_decoding_api="no"])
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
  ])
AS_IF([test "x$ac_cv_have_jpeg_decoding_api" = "xyes"], [USE_JPEG_DECODER=1])

dnl Check for va_dec_vp8.h header
saved_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $LIBVA_CFLAGS"
AC_CHECK_HEADERS([va/va_dec_vp8.h], [], [],
  [
#include <va/va.h>
  ])
CPPFLAGS="$saved_CPPFLAGS"

dnl Check for VP8 decoding API (0.34+)
USE_VP8_DECODER=0
AC_CACHE_CHECK([for VP8 decoding API],
  [ac_cv_have_vp8_decoding_api],
  [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $LIBVA_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $LIBVA_LIBS"
    AC_COMPILE_IFELSE(
      [
        AC_LANG_PROGRAM(
          [[
#include <va/va.h>
#ifdef HAVE_VA_VA_DEC_VP8_H
# include <va/va_dec_vp8.h>
#endif
          ]],
          [[
VAPictureParameterBufferVP8 pic_param;
VASliceParameterBufferVP8 slice_param;
VAProbabilityDataBufferVP8 prob_data;
VAIQMatrixBufferVP8 iq_matrix;
slice_param.slice_data_offset = 0;
slice_param.slice_data_flag = 0;
          ]])
      ],
      [ac_cv_have_vp8_decoding_api="yes"],
      [ac_cv_have_vp8_decoding_api="no"])
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
  ])
AS_IF([test "x$ac_cv_have_vp8_decoding_api" = "xyes"], [USE_VP8_DECODER=1])

dnl Check for va_dec_vp9.h header
saved_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $LIBVA_CFLAGS"
AC_CHECK_HEADERS([va/va_dec_vp9.h], [], [],
  [
#include <va/va.h>
  ])
CPPFLAGS="$saved_CPPFLAGS"

dnl Check for VP9 decoding API (0.37+)
USE_VP9_DECODER=0
AC_CACHE_CHECK([for VP9 decoding API],
  [ac_cv_have_vp9_decoding_api],
  [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $LIBVA_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $LIBVA_LIBS"
    AC_COMPILE_IFELSE(
      [
        AC_LANG_PROGRAM(
          [[
#include <va/va.h>
#ifdef HAVE_VA_VA_DEC_VP9_H
# include <va/va_dec_vp9.h>
#endif
          ]],
          [[
VADecPictureParameterBufferVP9 pic_param;
VASliceParameterBufferVP9 slice_param;
VASegmentParameterVP9 seg_param;
slice_param.slice_data_offset = 0;
slice_param.slice_data_flag = 0;
          ]])
      ],
      [ac_cv_have_vp9_decoding_api="yes"],
      [ac_cv_have_vp9_decoding_api="no"])
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
  ])
AS_IF([test "x$ac_cv_have_vp9_decoding_api" = "xyes"], [USE_VP9_DECODER=1])

dnl Check for va_dec_hevc.h header
saved_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $LIBVA_CFLAGS"
AC_CHECK_HEADERS([va/va_dec_hevc.h], [], [],
  [
#include <va/va.h>
  ])
CPPFLAGS="$saved_CPPFLAGS"

dnl Check for HEVC decoding API (0.38+)
USE_H265_DECODER=0
AC_CACHE_CHECK([for HEVC decoding API],
  [ac_cv_have_h265_decoding_api],
  [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $LIBVA_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $LIBVA_LIBS"
    AC_COMPILE_IFELSE(
      [
        AC_LANG_PROGRAM(
          [[
#include <va/va.h>
#ifdef HAVE_VA_VA_DEC_HEVC_H
# include <va/va_dec_hevc.h>
#endif
          ]],
          [[
VAPictureParameterBufferHEVC pic_param;
VASliceParameterBufferHEVC slice_param;
VAIQMatrixBufferHEVC iq_matrix;
slice_param.slice_data_offset = 0;
slice_param.slice_data_flag = 0;
          ]])
      ],
      [ac_cv_have_h265_decoding_api="yes"],
      [ac_cv_have_h265_decoding_api="no"])
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
  ])
AS_IF([test "x$ac_cv_have_h265_decoding_api" = "xyes"], [USE_H265_DECODER=1])

dnl Check for vpp (video post-processing) support
USE_VA_VPP=0
AC_CACHE_CHECK([for video post-postprocessing API],
  [ac_cv_have_va_vpp_api],
  [
    saved_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $LIBVA_CFLAGS"
    saved_LIBS="$LIBS"
    LIBS="$LIBS $LIBVA_LIBS"
    AC_COMPILE_IFELSE(
      [
        AC_LANG_PROGRAM(
          [[
#include <va/va.h>
#include <va/va_vpp.h>
          ]],
          [[
VADisplay va_dpy;
VAContextID vpp_ctx;
VAProcFilterType filters[VAProcFilterCount];
unsigned int num_filters = VAProcFilterCount;
vaQueryVideoProcFilters(va_dpy, vpp_ctx, filters, &num_filters);
          ]])
      ],
      [ac_cv_have_va_vpp_api="yes"],
      [ac_cv_have_va_vpp_api="no"])
    CPPFLAGS="$saved_CPPFLAGS"
    LIBS="$saved_LIBS"
  ])
AS_IF([test "x$ac_cv_have_va_vpp_api" = "xyes"], [USE_VA_VPP=1])

dnl Check for encoding support
USE_ENCODERS=0
if test "x$enable_encoders" = "xyes"; then
  PKG_CHECK_MODULES([LIBVA], [libva >= $VAAPI_ENC_REQ],
    [
      USE_ENCODERS=1
      saved_CPPFLAGS="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $LIBVA_CFLAGS"
      AC_CHECK_HEADERS([va/va_enc_mpeg2.h va/va_enc_h264.h], [],
        [USE_ENCODERS=0],
        [
#include <va/va.h>
        ])
      CPPFLAGS="$saved_CPPFLAGS"
    ], [:])
fi

USE_JPEG_ENCODER=0
USE_VP8_ENCODER=0
USE_H265_ENCODER=0
USE_VP9_ENCODER=0

if test $USE_ENCODERS -eq 1; then
  saved_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $LIBVA_CFLAGS"

  dnl Check for JPEG Encoding API (0.37.0+)
  AC_CHECK_HEADERS([va/va_enc_jpeg.h], [], [],
    [
#include <va/va.h>
    ])
  AC_CACHE_CHECK([for JPEG encoding API],
    [ac_cv_have_jpeg_encoding_api],
    [
      saved_LIBS="$LIBS"
      LIBS="$LIBS $LIBVA_LIBS"
      AC_COMPILE_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
#include <va/va.h>
#ifdef HAVE_VA_VA_ENC_JPEG_H
# include <va/va_enc_jpeg.h>
#endif
            ]],
            [[
VAEncPictureParameterBufferJPEG pic_param;
VAEncSliceParameterBufferJPEG slice_param;
VAQMatrixBufferJPEG q_matrix;
            ]])
        ],
        [ac_cv_have_jpeg_encoding_api="yes"],
        [ac_cv_have_jpeg_encoding_api="no"])
      LIBS="$saved_LIBS"
    ])
  AS_IF([test "x$ac_cv_have_jpeg_encoding_api" = "xyes"], [USE_JPEG_ENCODER=1])

  dnl Check for VP8 Encoding API
  AC_CHECK_HEADERS([va/va_enc_vp8.h], [], [],
    [
#include <va/va.h>
    ])
  AC_CACHE_CHECK([for VP8 encoding API],
    [ac_cv_have_vp8_encoding_api],
    [
      saved_LIBS="$LIBS"
      LIBS="$LIBS $LIBVA_LIBS"
      AC_COMPILE_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
#include <va/va.h>
#ifdef HAVE_VA_VA_ENC_VP8_H
# include <va/va_enc_vp8.h>
#endif
            ]],
            [[
VAEncSequenceParameterBufferVP8 seq_param;
VAEncPictureParameterBufferVP8 pic_param;
VAQMatrixBufferVP8 q_matrix;
            ]])
        ],
        [ac_cv_have_vp8_encoding_api="yes"],
        [ac_cv_have_vp8_encoding_api="no"])
      LIBS="$saved_LIBS"
    ])
  AS_IF([test "x$ac_cv_have_vp8_encoding_api" = "xyes"], [USE_VP8_ENCODER=1])

  dnl Check for H265/HEVC Encoding API
  AC_CHECK_HEADERS([va/va_enc_hevc.h], [], [],
    [
#include <va/va.h>
    ])
  AC_CACHE_CHECK([for HEVC encoding API],
    [ac_cv_have_hevc_encoding_api],
    [
      saved_LIBS="$LIBS"
      LIBS="$LIBS $LIBVA_LIBS"
      AC_COMPILE_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
#include <va/va.h>
#ifdef HAVE_VA_VA_ENC_HEVC_H
# include <va/va_enc_hevc.h>
#endif
            ]],
            [[
VAEncSequenceParameterBufferHEVC seq_param;
VAEncPictureParameterBufferHEVC pic_param;
VAEncSliceParameterBufferHEVC buf_param;
VAQMatrixBufferHEVC q_matrix;
            ]])
        ],
        [ac_cv_have_hevc_encoding_api="yes"],
        [ac_cv_have_hevc_encoding_api="no"])
      LIBS="$saved_LIBS"
    ])
  AS_IF([test "x$ac_cv_have_hevc_encoding_api" = "xyes"], [USE_H265_ENCODER=1])

  dnl Check for VP9 Encoding API
  AC_CHECK_HEADERS([va/va_enc_vp9.h], [], [],
    [
#include <va/va.h>
    ])
  AC_CACHE_CHECK([for VP9 encoding API],
    [ac_cv_have_vp9_encoding_api],
    [
      saved_LIBS="$LIBS"
      LIBS="$LIBS $LIBVA_LIBS"
      AC_COMPILE_IFELSE(
        [
          AC_LANG_PROGRAM(
            [[
#include <va/va.h>
#ifdef HAVE_VA_VA_ENC_VP9_H
# include <va/va_enc_vp9.h>
#endif
            ]],
            [[
VAEncSequenceParameterBufferVP9 seq_param;
VAEncPictureParameterBufferVP9 pic_param;
VAEncSegParamVP9 seg_param;
VAEncMiscParameterTypeVP9PerSegmantParam misc_param;
            ]])
        ],
        [ac_cv_have_vp9_encoding_api="yes"],
        [ac_cv_have_vp9_encoding_api="no"])
      LIBS="$saved_LIBS"
    ])
  AS_IF([test "x$ac_cv_have_vp9_encoding_api" = "xyes"], [USE_VP9_ENCODER=1])

  CPPFLAGS="$saved_CPPFLAGS"
fi

dnl VA/Wayland API
if test $USE_WAYLAND -eq 1; then
  PKG_CHECK_MODULES([LIBVA_WAYLAND], [libva-wayland >= $VAAPI_WLD_REQ],
    [], [USE_WAYLAND=0])
fi

dnl *** finalize CFLAGS, LDFLAGS, LIBS

# set by AG_GST_PARSE_SUBSYSTEM_DISABLES above
dnl make sure it doesn't complain about unused variables if debugging is disabled
AG_GST_CHECK_GST_DEBUG_DISABLED([NO_WARNINGS="-Wno-unused"], [NO_WARNINGS=""])

dnl define an ERROR_CFLAGS Makefile variable
dnl FIXME Add -Wredundant-decls again if considered useful and warnings are fixed
AG_GST_SET_ERROR_CFLAGS([$FATAL_WARNINGS], [-Wmissing-declarations -Wmissing-prototypes -Wwrite-strings -Wformat-security -Wold-style-definition -Winit-self -Wmissing-include-dirs -Waddress -Wno-multichar -Wnested-externs $NO_WARNINGS])

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT([$GST_GIT])

dnl Overview:
dnl GST_OPTION_CFLAGS:  common flags for profiling, debugging, errors, ...
dnl GST_*:              flags shared by built objects to link against GStreamer
dnl GST_ALL_LDFLAGS:    linker flags shared by all
dnl GST_LIB_LDFLAGS:    additional linker flags for all libaries
dnl GST_LT_LDFLAGS:     library versioning of our libraries
dnl GST_PLUGIN_LDFLAGS: flags to be used for all plugins

dnl GST_OPTION_CFLAGS
if test "x$USE_DEBUG" = xyes; then
   PROFILE_CFLAGS="-g"
fi
AC_SUBST([PROFILE_CFLAGS])

if test "x$GST_GIT" = "xyes"; then
  DEPRECATED_CFLAGS="-DGST_DISABLE_DEPRECATED"
else
  DEPRECATED_CFLAGS=""
fi
AC_SUBST([DEPRECATED_CFLAGS])

dnl every flag in GST_OPTION_CFLAGS and GST_OPTION_CXXFLAGS can be overridden
dnl at make time with e.g. make ERROR_CFLAGS=""
GST_OPTION_CFLAGS="\$(WARNING_CFLAGS) \$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(DEPRECATED_CFLAGS)"
AC_SUBST([GST_OPTION_CFLAGS])

dnl FIXME: do we want to rename to GST_ALL_* ?
dnl prefer internal headers to already installed ones
dnl also add builddir include for enumtypes and marshal
dnl add GST_OPTION_CFLAGS, but overridable
GST_CFLAGS="$GST_CFLAGS -DGST_USE_UNSTABLE_API"
GST_CFLAGS="$GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CFLAGS)"
AC_SUBST([GST_CFLAGS])

dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_ALL_LDFLAGS="-no-undefined"
if test "x${enable_Bsymbolic}" = "xyes"; then
  GST_ALL_LDFLAGS="$GST_ALL_LDFLAGS -Wl,-Bsymbolic-functions"
fi
AC_SUBST([GST_ALL_LDFLAGS])

dnl GST_LIB_LDFLAGS
dnl linker flags shared by all libraries
dnl LDFLAGS modifier defining exported symbols from built libraries
dnl (export _gst_foo but not __gst_foo)
GST_LIB_LDFLAGS="-export-symbols-regex ^_?\(gst_\|Gst\|GST_\).*"
AC_SUBST([GST_LIB_LDFLAGS])

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_.*' $GST_ALL_LDFLAGS"
AC_SUBST([GST_PLUGIN_LDFLAGS])

dnl ---------------------------------------------------------------------------
dnl -- Generate files and summary                                            --
dnl ---------------------------------------------------------------------------

case ":$USE_X11:$USE_GLX:$USE_EGL:$USE_WAYLAND:$USE_DRM:" in
*:1:*) ;;
*) AC_MSG_ERROR([No renderer is enabled]) ;;
esac

AC_DEFINE_UNQUOTED([USE_ENCODERS], [$USE_ENCODERS],
  [Defined to 1 if video encoders are used])
AM_CONDITIONAL([USE_ENCODERS], [test $USE_ENCODERS -eq 1])

AC_DEFINE_UNQUOTED([USE_JPEG_ENCODER], [$USE_JPEG_ENCODER],
  [Defined to 1 if JPEG encoder is used])
AM_CONDITIONAL([USE_JPEG_ENCODER], [test $USE_JPEG_ENCODER -eq 1])

AC_DEFINE_UNQUOTED([USE_VP8_ENCODER], [$USE_VP8_ENCODER],
  [Defined to 1 if VP8 encoder is used])
AM_CONDITIONAL([USE_VP8_ENCODER], [test $USE_VP8_ENCODER -eq 1])

AC_DEFINE_UNQUOTED([USE_H265_ENCODER], [$USE_H265_ENCODER],
  [Defined to 1 if H265 encoder is used])
AM_CONDITIONAL([USE_H265_ENCODER], [test $USE_H265_ENCODER -eq 1])

AC_DEFINE_UNQUOTED([USE_VP9_ENCODER], [$USE_VP9_ENCODER],
  [Defined to 1 if VP9 encoder is used])
AM_CONDITIONAL([USE_VP9_ENCODER], [test $USE_VP9_ENCODER -eq 1])

AC_DEFINE_UNQUOTED([USE_VA_VPP], [$USE_VA_VPP],
  [Defined to 1 if video post-processing is used])
AM_CONDITIONAL([USE_VA_VPP], [test $USE_VA_VPP -eq 1])

AC_DEFINE_UNQUOTED([USE_JPEG_DECODER], [$USE_JPEG_DECODER],
  [Defined to 1 if JPEG decoder is used])
AM_CONDITIONAL([USE_JPEG_DECODER], [test $USE_JPEG_DECODER -eq 1])

AC_DEFINE_UNQUOTED([USE_VP8_DECODER], [$USE_VP8_DECODER],
  [Defined to 1 if VP8 decoder is used])
AM_CONDITIONAL([USE_VP8_DECODER], [test $USE_VP8_DECODER -eq 1])

AC_DEFINE_UNQUOTED([USE_VP9_DECODER], [$USE_VP9_DECODER],
  [Defined to 1 if VP9 decoder is used])
AM_CONDITIONAL([USE_VP9_DECODER], [test $USE_VP9_DECODER -eq 1])

AC_DEFINE_UNQUOTED([USE_H265_DECODER], [$USE_H265_DECODER],
  [Defined to 1 if HEVC decoder is used])
AM_CONDITIONAL([USE_H265_DECODER], [test $USE_H265_DECODER -eq 1])

AC_DEFINE_UNQUOTED([USE_DRM], [$USE_DRM],
  [Defined to 1 if DRM is enabled])
AM_CONDITIONAL([USE_DRM], [test $USE_DRM -eq 1])

AC_DEFINE_UNQUOTED([USE_X11], [$USE_X11],
  [Defined to 1 if X11 is enabled])
AM_CONDITIONAL([USE_X11], [test $USE_X11 -eq 1])

AC_DEFINE_UNQUOTED([USE_GLX], [$USE_GLX],
  [Defined to 1 if GLX is enabled])
AM_CONDITIONAL([USE_GLX], [test $USE_GLX -eq 1])

AC_DEFINE_UNQUOTED([USE_EGL], [$USE_EGL],
  [Defined to 1 if EGL is enabled])
AM_CONDITIONAL([USE_EGL], [test $USE_EGL -eq 1])

AC_DEFINE_UNQUOTED([USE_GLES_VERSION_MASK], [$GLES_VERSION_MASK],
  [Defined to the set of enabled OpenGL ES APIs])

AC_DEFINE_UNQUOTED([USE_WAYLAND], [$USE_WAYLAND],
  [Defined to 1 if WAYLAND is enabled])
AM_CONDITIONAL([USE_WAYLAND], [test $USE_WAYLAND -eq 1])

dnl @TODO hack for egl's dynamic module loading. remove it!
AC_DEFINE_UNQUOTED([GST_VAAPI_MAJOR_VERSION_S], ["0"],
  [Defined to the string representation of gstreamer-vaapi major version])

AC_CONFIG_FILES([
    Makefile
    common/Makefile
    common/m4/Makefile
    m4/Makefile
    docs/Makefile
    docs/version.entities
    docs/plugins/Makefile
    docs/plugins/gstreamer-vaapi-plugins-docs.xml
    gst-libs/Makefile
    gst-libs/gst/Makefile
    gst-libs/gst/base/Makefile
    gst-libs/gst/vaapi/Makefile
    gst/Makefile
    gst/vaapi/Makefile
    tests/Makefile
    tests/elements/Makefile
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
AS_IF([test $USE_EGL -eq 1], [VIDEO_OUTPUTS="$VIDEO_OUTPUTS egl"])
AS_IF([test $USE_WAYLAND -eq 1], [VIDEO_OUTPUTS="$VIDEO_OUTPUTS wayland"])

echo
echo $PACKAGE configuration summary:
echo
echo Installation Prefix .............. : ${prefix}
echo GStreamer API version ............ : $GST_VERSION
echo VA-API version ................... : $VA_VERSION_STR
echo Video encoding ................... : $(yesno $USE_ENCODERS)
echo Video outputs .................... : $VIDEO_OUTPUTS
echo
