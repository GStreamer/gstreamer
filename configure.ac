# gstreamer-vaapi package version number
m4_define([gst_vaapi_major_version], [0])
m4_define([gst_vaapi_minor_version], [2])
m4_define([gst_vaapi_micro_version], [5])
m4_define([gst_vaapi_version],
          [gst_vaapi_major_version.gst_vaapi_minor_version.gst_vaapi_micro_version])

# gst version number
m4_define([gst_major_version], [0])
m4_define([gst_minor_version], [10])
m4_define([gst_micro_version], [10])
m4_define([gst_major_minor_version],
          [gst_major_version.gst_minor_version])
m4_define([gst_version],
          [gst_major_version.gst_minor_version.gst_micro_version])

# gst plugins-base version number
m4_define([gst_plugins_base_major_version], [0])
m4_define([gst_plugins_base_minor_version], [10])
m4_define([gst_plugins_base_micro_version], [16])
m4_define([gst_plugins_base_version],
          [gst_plugins_base_major_version.gst_plugins_base_minor_version.gst_plugins_base_micro_version])

# libva package version number
m4_define([libva_sds_version_0_29], [8])
m4_define([libva_sds_package_version_0_29], [0.29-2+sds11])
m4_define([libva_sds_version_0_30], [1])
m4_define([libva_sds_package_version_0_30], [0.30-1+sds1])
m4_define([libva_glx_sds_version_0_30], [5])
m4_define([libva_glx_sds_package_version_0_30], [0.30.4-1+sds6])
m4_define([libva_glx_sds_version_0_31], [1])
m4_define([libva_glx_sds_package_version_0_31], [0.31.0-1+sds1])
m4_define([libva_sds_version], [libva_glx_sds_version_0_31])
m4_define([libva_sds_package_version], [libva_glx_sds_package_version_0_31])

# gtk-doc version number
# XXX: introspection annotations require gtk-doc >= 1.12
m4_define([gtkdoc_major_version], [1])
m4_define([gtkdoc_minor_version], [9])
m4_define([gtkdoc_version], [gtkdoc_major_version.gtkdoc_minor_version])

AC_PREREQ([2.57])
AC_INIT([gst_vaapi], [gst_vaapi_version],
    [gbeauchesne@splitted-desktop.com],
    [gstreamer-vaapi])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CANONICAL_TARGET
AM_INIT_AUTOMAKE
AM_CONFIG_HEADER([config.h])

TODAY="`LC_ALL=C date +'%a, %d %b %Y %X %z'`"
AC_SUBST(TODAY)

GST_VAAPI_MAJOR_VERSION=gst_vaapi_major_version
AC_SUBST(GST_VAAPI_MAJOR_VERSION)

LIBVA_SDS_PACKAGE_VERSION=libva_sds_package_version
AC_SUBST(LIBVA_SDS_PACKAGE_VERSION)

dnl Versions for GStreamer and plugins-base
GST_MAJORMINOR=gst_major_minor_version
GST_VERSION_REQUIRED=gst_version
GST_PLUGINS_BASE_VERSION_REQUIRED=gst_plugins_base_version
AC_SUBST(GST_MAJORMINOR)
AC_SUBST(GST_VERSION_REQUIRED)
AC_SUBST(GST_PLUGINS_BASE_VERSION_REQUIRED)

dnl Check for tools
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_LIBTOOL

AC_ARG_ENABLE(glx,
    AC_HELP_STRING([--enable-glx],
                   [enable OpenGL/X11 @<:@default=yes@:>@]),
    [], [enable_glx="yes"])

AC_ARG_ENABLE(vaapi-glx,
    AC_HELP_STRING([--enable-vaapi-glx],
                   [enable VA/GLX extensions @<:@default=yes@:>@]),
    [], [enable_vaapi_glx="yes"])

AC_ARG_ENABLE(vaapisink-glx,
    AC_HELP_STRING([--enable-vaapisink-glx],
                   [enable OpenGL/X11 to vaapisink @<:@default=yes@:>@]),
    [], [enable_vaapisink_glx="no"])

dnl Check for __attribute__((visibility()))
AC_CACHE_CHECK([whether __attribute__((visibility())) is supported],
  vaapi_cv_visibility_attribute,
  [cat > conftest.c <<EOF
int foo __attribute__ ((visibility ("hidden"))) = 1;
int bar __attribute__ ((visibility ("protected"))) = 1;
EOF
  vaapi_cv_visibility_attribute=no
  if ${CC-cc} -Werror -S conftest.c -o conftest.s >/dev/null 2>&1; then
    if grep '\.hidden.*foo' conftest.s >/dev/null; then
      if grep '\.protected.*bar' conftest.s >/dev/null; then
	vaapi_cv_visibility_attribute=yes
      fi
    fi
  fi
  rm -f conftest.[cs]
])
if test $vaapi_cv_visibility_attribute = yes; then
  vaapi_cv_visibility_attribute_hidden="__attribute__((visibility(\"hidden\")))"
else
  vaapi_cv_visibility_attribute_hidden=""
fi
AC_DEFINE_UNQUOTED([attribute_hidden],
                   [$vaapi_cv_visibility_attribute_hidden],
                   [Define the "hidden" visibility attribute])

dnl Check for basic libraries
AC_CHECK_LIB(m, tan)

dnl Check for Gtk doc
GTKDOC_VERSION=gtkdoc_version
GTK_DOC_CHECK([$GTKDOC_VERSION])
AM_CONDITIONAL([BUILD_GTK_DOC], [test "x$enable_gtk_doc" = "xyes"])
AC_SUBST(GTKDOC_VERSION)

dnl Check for GLib
AC_PATH_PROG([GLIB_GENMARSHAL], [glib-genmarshal])
PKG_CHECK_MODULES([GLIB], [glib-2.0])
AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)

dnl Check for GStreamer
PKG_CHECK_MODULES([GST],
    [gstreamer-$GST_MAJORMINOR >= $GST_VERSION_REQUIRED]
)
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

dnl Check for GStreamer plugins-base
PKG_CHECK_MODULES([GST_PLUGINS_BASE],
    [gstreamer-plugins-base-$GST_MAJORMINOR >= $GST_PLUGINS_BASE_VERSION_REQUIRED]
)
AC_SUBST(GST_PLUGINS_BASE_CFLAGS)
AC_SUBST(GST_PLUGINS_BASE_LIBS)

dnl Check for GStreamer base
PKG_CHECK_MODULES([GST_BASE],
    [gstreamer-base-$GST_MAJORMINOR >= $GST_VERSION_REQUIRED]
)
AC_SUBST(GST_BASE_CFLAGS)
AC_SUBST(GST_BASE_LIBS)

dnl Check for GStreamer video
PKG_CHECK_MODULES([GST_VIDEO],
    [gstreamer-video-$GST_MAJORMINOR >= $GST_VERSION_REQUIRED]
)
AC_SUBST(GST_VIDEO_CFLAGS)
AC_SUBST(GST_VIDEO_LIBS)

dnl Check for GStreamer interfaces
PKG_CHECK_MODULES([GST_INTERFACES],
    [gstreamer-interfaces-$GST_MAJORMINOR >= $GST_PLUGINS_BASE_VERSION_REQUIRED]
)
AC_SUBST(GST_INTERFACES_CFLAGS)
AC_SUBST(GST_INTERFACES_LIBS)

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
    AC_TRY_LINK(
    [#include <va_x11.h>],
    [vaGetDisplay(NULL)],
    [ac_cv_have_vaapi_old="yes"],
    [ac_cv_have_vaapi_old="no"])
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

dnl Check for OpenGL support to vaapisink
if test "$enable_vaapisink_glx:$USE_GLX" = "yes:1"; then
    USE_VAAPISINK_GLX=1
else
    USE_VAAPISINK_GLX=0
fi

dnl Check for FFmpeg
PKG_CHECK_MODULES(LIBAVCODEC, [libavcodec])
AC_CHECK_HEADERS([libavcodec/avcodec.h ffmpeg/avcodec.h])
AC_CHECK_HEADERS([libavcodec/vaapi.h ffmpeg/vaapi.h], [break],
    AC_MSG_ERROR([The system FFmpeg headers do not support VA-API]))

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
VA_SDS_VERSION=`$PKG_CONFIG libva --variable sdsversion`

VA_VERSION_STR="$VA_VERSION"
if test -n "$VA_SDS_VERSION"; then
    VA_VERSION_STR="$VA_VERSION_STR-sds$VA_SDS_VERSION"
fi

pkgconfigdir=${libdir}/pkgconfig
AC_SUBST(pkgconfigdir)

AC_OUTPUT([
    Makefile
    debian.upstream/Makefile
    debian.upstream/changelog
    debian.upstream/control
    debian.upstream/gstreamer$GST_MAJORMINOR-vaapi.install:\
debian.upstream/gstreamer-vaapi.install.in
    debian.upstream/gstreamer$GST_MAJORMINOR-vaapi-doc.install:\
debian.upstream/gstreamer-vaapi-doc.install.in
    debian.upstream/libgstvaapi$GST_VAAPI_MAJOR_VERSION.install:\
debian.upstream/libgstvaapi.install.in
    debian.upstream/libgstvaapi-dev.install
    debian.upstream/libgstvaapi-x11-$GST_VAAPI_MAJOR_VERSION.install:\
debian.upstream/libgstvaapi-x11.install.in
    debian.upstream/libgstvaapi-glx-$GST_VAAPI_MAJOR_VERSION.install:\
debian.upstream/libgstvaapi-glx.install.in
    docs/Makefile
    docs/reference/Makefile
    docs/reference/libs/Makefile
    docs/reference/libs/libs-docs.xml
    docs/reference/plugins/Makefile
    docs/reference/plugins/plugins-docs.xml
    gst-libs/Makefile
    gst-libs/gst/Makefile
    gst-libs/gst/vaapi/Makefile
    pkgconfig/Makefile
    pkgconfig/gstreamer-vaapi-$GST_MAJORMINOR.pc:\
pkgconfig/gstreamer-vaapi.pc.in
    pkgconfig/gstreamer-vaapi-glx-$GST_MAJORMINOR.pc:\
pkgconfig/gstreamer-vaapi-glx.pc.in
    pkgconfig/gstreamer-vaapi-x11-$GST_MAJORMINOR.pc:\
pkgconfig/gstreamer-vaapi-x11.pc.in
    gst/Makefile
    gst/vaapiconvert/Makefile
    gst/vaapidecode/Makefile
    gst/vaapisink/Makefile
    tests/Makefile
])

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
