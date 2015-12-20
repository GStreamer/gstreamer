AC_PREREQ(2.62)

dnl please read gstreamer/docs/random/autotools before changing this file

dnl initialize autoconf
dnl releases only do -Wall, git and prerelease does -Werror too
dnl use a three digit version number for releases, and four for git/prerelease
AC_INIT(GStreamer OpenMAX Plug-ins, 1.2.0,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gst-omx)

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability 1.11 no-dist-gzip dist-xz tar-ustar])

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_GIT="no", GST_GIT="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([omx/gstomx.c])

dnl define the output header for config
AC_CONFIG_HEADERS([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE([enable])

dnl sets host_* variables
AC_CANONICAL_HOST

dnl use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST(AM_DEFAULT_VERBOSITY)])

dnl our libraries and install dirs use GST_API_VERSION in the filename
dnl to allow side-by-side installation of different API versions
GST_API_VERSION=1.0
AC_SUBST(GST_API_VERSION)
AC_DEFINE_UNQUOTED(GST_API_VERSION, "$GST_API_VERSION",
  [GStreamer API Version])

AG_GST_LIBTOOL_PREPARE
AS_LIBTOOL(GST, 0, 0, 0)

dnl *** required versions of GStreamer stuff ***
GST_REQ=1.2.2

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl *** check for arguments to configure ***

AG_GST_ARG_DISABLE_FATAL_WARNINGS

AG_GST_ARG_DEBUG
AG_GST_ARG_PROFILING
AG_GST_ARG_VALGRIND
AG_GST_ARG_GCOV

AG_GST_ARG_EXAMPLES

AG_GST_ARG_WITH_PKG_CONFIG_PATH
AG_GST_ARG_WITH_PACKAGE_NAME
AG_GST_ARG_WITH_PACKAGE_ORIGIN

AG_GST_PKG_CONFIG_PATH

AG_GST_ARG_WITH_PLUGINS

AG_GST_ARG_ENABLE_EXPERIMENTAL

dnl *** checks for platform ***

dnl * hardware/architecture *

dnl common/m4/gst-arch.m4
dnl check CPU type
AG_GST_ARCH

dnl check for large file support
dnl affected plugins must include config.h
AC_SYS_LARGEFILE

dnl *** checks for programs ***

dnl find a compiler
AC_PROG_CC
AC_PROG_CC_STDC

dnl check if the compiler supports '-c' and '-o' options
AM_PROG_CC_C_O

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for documentation tools
GTK_DOC_CHECK([1.3])
AS_PATH_PYTHON([2.1])
AG_GST_PLUGIN_DOCS([1.3],[2.1])

dnl *** checks for libraries ***

dnl libm, for sin() etc.
LT_LIB_M
AC_SUBST(LIBM)

dnl *** checks for header files ***

dnl check if we have ANSI C header files
AC_HEADER_STDC

AX_CREATE_STDINT_H

dnl *** checks for functions ***

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***

dnl Check for a way to display the function name in debug output
AG_GST_CHECK_FUNCTION

dnl *** checks for dependency libraries ***

dnl GLib is required
AG_GST_GLIB_CHECK([2.32])

dnl Needed by plugins that use g_module_*() API
PKG_CHECK_MODULES(GMODULE_NO_EXPORT, gmodule-no-export-2.0)

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_BASE($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_CONTROLLER($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_CHECK($GST_API_VERSION, [$GST_REQ], no)
AG_GST_CHECK_GST_PLUGINS_BASE($GST_API_VERSION, [$GST_REQ], yes)
AM_CONDITIONAL(HAVE_GST_CHECK, test "x$HAVE_GST_CHECK" = "xyes")
PKG_CHECK_MODULES([GST_GL], [gstreamer-gl-1.0 >= 1.4.0], [
  AC_DEFINE(HAVE_GST_GL, 1, [Have gstreamer-gl])
  GST_GL=yes
], [GST_GL=no])
AM_CONDITIONAL(HAVE_GST_GL, test "x$GST_GL" = "xyes")

dnl Check for documentation xrefs
GLIB_PREFIX="`$PKG_CONFIG --variable=prefix glib-2.0`"
GST_PREFIX="`$PKG_CONFIG --variable=prefix gstreamer-$GST_API_VERSION`"
AC_SUBST(GLIB_PREFIX)
AC_SUBST(GST_PREFIX)

AC_ARG_WITH([omx-header-path],
            AS_HELP_STRING([--with-omx-header-path],[path of external OpenMAX IL header files]),
        [omx_header_path="$withval"], [omx_header_path="none"])

if test x"$omx_header_path" != x"none"; then
    CPPFLAGS="$CPPFLAGS -I$omx_header_path"
fi
dnl Check for external OpenMAX IL headers
AC_CHECK_HEADER([OMX_Core.h], [HAVE_EXTERNAL_OMX=yes], [HAVE_EXTERNAL_OMX=no], [AC_INCLUDES_DEFAULT])
AM_CONDITIONAL(HAVE_EXTERNAL_OMX, test "x$HAVE_EXTERNAL_OMX" = "xyes")

dnl Our internal OpenMAX IL headers have OMX_VideoExt.h
HAVE_VIDEO_EXT=yes
if test "x$HAVE_EXTERNAL_OMX" = "xyes"; then
    AC_CHECK_HEADER([OMX_VideoExt.h], [HAVE_VIDEO_EXT=yes], [HAVE_VIDEO_EXT=no], [AC_INCLUDES_DEFAULT])
fi

VIDEO_HEADERS="#include <OMX_Video.h>"
if test "x$HAVE_VIDEO_EXT" = "xyes"; then
    AC_DEFINE(HAVE_VIDEO_EXT, 1, [OpenMAX IL has OMX_VideoExt.h header])
    VIDEO_HEADERS="$VIDEO_HEADERS
#include <OMX_VideoExt.h>
"
fi

AC_CHECK_DECLS([OMX_VIDEO_CodingVP8],
  [
    AC_DEFINE(HAVE_VP8, 1, [OpenMAX IL has VP8 support])
    HAVE_VP8=yes
  ], [
    HAVE_VP8=no
  ], [[$VIDEO_HEADERS]])
AM_CONDITIONAL(HAVE_VP8, test "x$HAVE_VP8" = "xyes")

AC_CHECK_DECLS([OMX_VIDEO_CodingTheora],
  [
    AC_DEFINE(HAVE_THEORA, 1, [OpenMAX IL has Theora support])
    HAVE_THEORA=yes
  ], [
    HAVE_THEORA=no
  ], [[$VIDEO_HEADERS]])
AM_CONDITIONAL(HAVE_THEORA, test "x$HAVE_THEORA" = "xyes")

dnl Check for -Bsymbolic-functions linker flag used to avoid
dnl intra-library PLT jumps, if available.
AC_ARG_ENABLE(Bsymbolic,
              [AS_HELP_STRING([--disable-Bsymbolic],[avoid linking with -Bsymbolic])],,
              [SAVED_LDFLAGS="${LDFLAGS}" SAVED_LIBS="${LIBS}"
               AC_MSG_CHECKING([for -Bsymbolic-functions linker flag])
               LDFLAGS=-Wl,-Bsymbolic-functions
               LIBS=
               AC_TRY_LINK([], [return 0],
                           AC_MSG_RESULT(yes)
                           enable_Bsymbolic=yes,
                           AC_MSG_RESULT(no)
                           enable_Bsymbolic=no)
               LDFLAGS="${SAVED_LDFLAGS}" LIBS="${SAVED_LIBS}"])

AC_ARG_WITH([omx-target],
        AS_HELP_STRING([--with-omx-target],[Use this OpenMAX IL target (generic, bellagio, rpi)]),
        [ac_cv_omx_target="$withval"], [ac_cv_omx_target="none"])

ac_cv_omx_target_struct_packing="none"
AC_MSG_NOTICE([Using $ac_cv_omx_target as OpenMAX IL target])
case "${ac_cv_omx_target}" in
  generic)
    AC_DEFINE(USE_OMX_TARGET_GENERIC, 1, [Use generic OpenMAX IL target])
    ;;
  rpi)
    AC_DEFINE(USE_OMX_TARGET_RPI, 1, [Use RPi OpenMAX IL target])
    ac_cv_omx_target_struct_packing=4
    ;;
  bellagio)
    AC_DEFINE(USE_OMX_TARGET_BELLAGIO, 1, [Use Bellagio OpenMAX IL target])
    ;;
  none|*)
    AC_ERROR([invalid OpenMAX IL target, you must specify one of --with-omx-target={generic,rpi,bellagio}])
    ;;
esac
AM_CONDITIONAL(USE_OMX_TARGET_GENERIC, test "x$ac_cv_omx_target" = "xgeneric")
AM_CONDITIONAL(USE_OMX_TARGET_BELLAGIO, test "x$ac_cv_omx_target" = "xbellagio")
AM_CONDITIONAL(USE_OMX_TARGET_RPI, test "x$ac_cv_omx_target" = "xrpi")

AC_ARG_WITH([omx-struct-packing],
        AS_HELP_STRING([--with-omx-struct-packing],[Force OpenMAX struct packing, (default is none)]),
        [ac_cv_omx_struct_packing="$withval"], [ac_cv_omx_struct_packing="none"])

if test x"$ac_cv_omx_struct_packing" != x"none"; then
  AC_MSG_NOTICE([Using $ac_cv_omx_struct_packing as OpenMAX struct packing])
  AC_DEFINE_UNQUOTED(GST_OMX_STRUCT_PACKING, $ac_cv_omx_struct_packing, [The struct packing used for OpenMAX structures])
elif test x"$ac_cv_omx_target_struct_packing" != x"none"; then
  AC_MSG_NOTICE([Using $ac_cv_omx_target_struct_packing as OpenMAX struct packing])
  AC_DEFINE_UNQUOTED(GST_OMX_STRUCT_PACKING, $ac_cv_omx_target_struct_packing, [The struct packing used for OpenMAX structures])
fi

dnl *** set variables based on configure arguments ***

dnl set license and copyright notice
GST_LICENSE="LGPL"
AC_DEFINE_UNQUOTED(GST_LICENSE, "$GST_LICENSE", [GStreamer license])
AC_SUBST(GST_LICENSE)

dnl set location of plugin directory
AG_GST_SET_PLUGINDIR

dnl set release date/time
AG_GST_SET_PACKAGE_RELEASE_DATETIME_WITH_NANO([$PACKAGE_VERSION_NANO],
  ["${srcdir}/gst-omx.doap"],
  [$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR.$PACKAGE_VERSION_MICRO])

dnl build static plugins or not
AC_MSG_CHECKING([whether to build static plugins or not])
AC_ARG_ENABLE(
  static-plugins,
  AC_HELP_STRING(
    [--enable-static-plugins],
    [build static plugins @<:@default=no@:>@]),
  [AS_CASE(
    [$enableval], [no], [], [yes], [],
    [AC_MSG_ERROR([bad value "$enableval" for --enable-static-plugins])])],
  [enable_static_plugins=no])
AC_MSG_RESULT([$enable_static_plugins])
if test "x$enable_static_plugins" = xyes; then
  AC_DEFINE(GST_PLUGIN_BUILD_STATIC, 1,
    [Define if static plugins should be built])
  GST_PLUGIN_LIBTOOLFLAGS=""
else
  GST_PLUGIN_LIBTOOLFLAGS="--tag=disable-static"
fi
AC_SUBST(GST_PLUGIN_LIBTOOLFLAGS)
AM_CONDITIONAL(GST_PLUGIN_BUILD_STATIC, test "x$enable_static_plugins" = "xyes")

dnl define an ERROR_CFLAGS Makefile variable
AG_GST_SET_ERROR_CFLAGS($FATAL_WARNINGS, [
    -Wmissing-declarations -Wmissing-prototypes -Wredundant-decls -Wundef
    -Wwrite-strings -Wformat-nonliteral -Wformat-security -Wformat-nonliteral
    -Winit-self -Wmissing-include-dirs -Waddress -Waggregate-return
    -Wno-multichar -Wnested-externs ])

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT($GST_GIT)

dnl used in examples
AG_GST_DEFAULT_ELEMENTS

dnl *** plug-ins to include ***

dnl these are all the gst plug-ins, compilable without additional libs
AG_GST_CHECK_PLUGIN(omx)

dnl check for gstreamer core features (subsystems)
dnl FIXME: this assumes srcdir == builddir for uninstalled setups
GST_CONFIGPATH=`$PKG_CONFIG --variable=includedir gstreamer-$GST_API_VERSION`"/gst/gstconfig.h"
AG_GST_PARSE_SUBSYSTEM_DISABLES($GST_CONFIGPATH)
dnl AM_CONDITIONAL(USE_FOO, test $GST_DISABLE_FOO != "1")

dnl *** finalize CFLAGS, LDFLAGS, LIBS

dnl Overview:
dnl GST_OPTION_CFLAGS:  common flags for profiling, debugging, errors, ...
dnl GST_*:              flags shared by built objects to link against GStreamer
dnl GST_PLUGINS_BASE_CFLAGS: to link internally against the plugins base libs
dnl                          (compare to other modules) or for i18n
dnl GST_ALL_LDFLAGS:    linker flags shared by all
dnl GST_LIB_LDFLAGS:    additional linker flags for all libaries
dnl GST_LT_LDFLAGS:     library versioning of our libraries
dnl GST_PLUGIN_LDFLAGS: flags to be used for all plugins

dnl GST_OPTION_CFLAGS
if test "x$USE_DEBUG" = xyes; then
   PROFILE_CFLAGS="-g"
fi
AC_SUBST(PROFILE_CFLAGS)

if test "x$PACKAGE_VERSION_NANO" = "x1"; then
  dnl Define _only_ when compiling a git version (not pre-releases or releases)
  DEPRECATED_CFLAGS="-DGST_DISABLE_DEPRECATED"
else
  DEPRECATED_CFLAGS=""
fi
AC_SUBST(DEPRECATED_CFLAGS)

dnl every flag in GST_OPTION_CFLAGS and GST_OPTION_CXXFLAGS can be overridden
dnl at make time with e.g. make ERROR_CFLAGS=""
GST_OPTION_CFLAGS="\$(WARNING_CFLAGS) \$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
AC_SUBST(GST_OPTION_CFLAGS)

dnl GST_PLUGINS_BASE_CFLAGS
dnl prefer internal headers to already installed ones
dnl also add builddir include for enumtypes and marshal
GST_OMX_CFLAGS=""
AC_SUBST(GST_OMX_CFLAGS)

dnl FIXME: do we want to rename to GST_ALL_* ?
dnl add GST_OPTION_CFLAGS, but overridable
GST_CFLAGS="$GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CFLAGS)"
AC_SUBST(GST_CFLAGS)
dnl add GCOV libs because libtool strips -fprofile-arcs -ftest-coverage
GST_LIBS="$GST_LIBS \$(GCOV_LIBS)"
AC_SUBST(GST_LIBS)

dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_ALL_LDFLAGS="-no-undefined"
if test "x${enable_Bsymbolic}" = "xyes"; then
  GST_ALL_LDFLAGS="$GST_ALL_LDFLAGS -Wl,-Bsymbolic-functions"
fi
AC_SUBST(GST_ALL_LDFLAGS)

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_.*' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl *** output files ***

AC_CONFIG_FILES(
Makefile
omx/Makefile
common/Makefile
common/m4/Makefile
tools/Makefile
config/Makefile
config/bellagio/Makefile
config/rpi/Makefile
examples/Makefile
examples/egl/Makefile
m4/Makefile
)

AC_OUTPUT

