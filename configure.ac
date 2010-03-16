# gst version number
m4_define([gst_major_version], [0])
m4_define([gst_minor_version], [10])
m4_define([gst_micro_version], [0])
m4_define([gst_major_minor_version],
          [gst_major_version.gst_minor_version])
m4_define([gst_version],
          [gst_major_version.gst_minor_version.gst_micro_version])

# gst plugins-base version number
m4_define([gst_plugins_base_major_version], [0])
m4_define([gst_plugins_base_minor_version], [10])
m4_define([gst_plugins_base_micro_version], [0])
m4_define([gst_plugins_base_version],
          [gst_plugins_base_major_version.gst_plugins_base_minor_version.gst_plugins_base_micro_version])

# gst_vaapi package version number
m4_define([gst_vaapi_major_version], [0])
m4_define([gst_vaapi_minor_version], [1])
m4_define([gst_vaapi_micro_version], [0])
m4_define([gst_vaapi_version],
          [gst_vaapi_major_version.gst_vaapi_minor_version.gst_vaapi_micro_version])

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

dnl Versions for GStreamer and plugins-base
GST_MAJORMINOR=gst_major_minor_version
GST_VERSION_REQUIRED=gst_version
GST_PLUGINS_BASE_VERSION_REQUIRED=gst_plugins_base_version
AC_SUBST(GST_MAJORMINOR)

dnl Check for tools
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_LIBTOOL

dnl Check for GLib
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

dnl Check for VA-API
PKG_CHECK_MODULES(LIBVA, [libva])
PKG_CHECK_MODULES(X11, [x11])
PKG_CHECK_MODULES(LIBVA_X11, [libva-x11])

dnl Check for SDS extensions to VA-API
AC_CACHE_CHECK([for VA-API],
  ac_cv_libva_sds_extensions, [
  saved_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $LIBVA_DEPS_CFLAGS"
  AC_TRY_COMPILE([
    #include <va/va_version.h>
    #if VA_MAJOR_VERSION == 0 && VA_MINOR_VERSION == 29
    # if !defined(VA_SDS_VERSION) || (VA_SDS_VERSION < $LIBVA_SDS_VERSION_0_29)
    #  error "VA-API version >= 0.29.0-sds$LIBVA_SDS_VERSION_0_29 is required"
    # endif
    #elif VA_MAJOR_VERSION == 0 && VA_MINOR_VERSION == 30
    # if !defined(VA_SDS_VERSION) || (VA_SDS_VERSION < $LIBVA_SDS_VERSION_0_30)
    #  error "VA-API version >= 0.30.0-sds$LIBVA_SDS_VERSION_0_30 is required"
    # endif
    #elif !VA_CHECK_VERSION(0,31,0)
    # error "VA-API version >= 0.31 is required"
    #endif
  ], [],
  [ac_cv_libva_sds_extensions="yes"],
  [ac_cv_libva_sds_extensions="no"])
  CFLAGS="$saved_CFLAGS"
])
VA_VERSION=`$PKG_CONFIG --modversion libva`
VA_MAJOR_VERSION=`echo "$VA_VERSION" | cut -d'.' -f1`
VA_MINOR_VERSION=`echo "$VA_VERSION" | cut -d'.' -f2`
VA_DRIVER_INIT_FUNC="__vaDriverInit_${VA_MAJOR_VERSION}_${VA_MINOR_VERSION}_sds"
if test "$ac_cv_libva_sds_extensions" = "yes"; then
    AC_DEFINE_UNQUOTED([VA_DRIVER_INIT_FUNC], [$VA_DRIVER_INIT_FUNC], [Define driver entry-point])
else
    AC_MSG_ERROR([Your VA-API SDK does not include SDS extensions])
fi

pkgconfigdir=${libdir}/pkgconfig
AC_SUBST(pkgconfigdir)

AC_OUTPUT([
    Makefile
    debian/Makefile
    debian/changelog
    debian/control
    debian/gstreamer$GST_MAJORMINOR-vaapi.install:debian/gstreamer-vaapi.install.in
    debian/libgstvaapi$GST_VAAPI_MAJOR_VERSION.install:debian/libgstvaapi.install.in
    debian/libgstvaapi$GST_VAAPI_MAJOR_VERSION-dev.install:debian/libgstvaapi-dev.install.in
    debian/libgstvaapi-x11-$GST_VAAPI_MAJOR_VERSION.install:debian/libgstvaapi-x11.install.in
    gst-libs/Makefile
    gst-libs/gst/Makefile
    gst-libs/gst/vaapi/Makefile
    pkgconfig/Makefile
    pkgconfig/gstreamer-vaapi.pc
    pkgconfig/gstreamer-vaapi-x11.pc
    sys/Makefile
    sys/vaapiconvert/Makefile
    sys/vaapisink/Makefile
    tests/Makefile
])
