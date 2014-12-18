AC_PREREQ([2.68])

dnl please read gstreamer/docs/random/autotools before changing this file

dnl initialize autoconf
dnl releases only do -Wall, git and prerelease does -Werror too
dnl use a three digit version number for releases, and four for git/prerelease
AC_INIT([GStreamer Base Plug-ins],[1.4.5],[http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer],[gst-plugins-base])

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability 1.11 no-dist-gzip dist-xz tar-ustar subdir-objects])

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_GIT="no", GST_GIT="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/audiotestsrc/gstaudiotestsrc.c])

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

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
dnl
dnl Keep CURRENT as MINOR * 100 + MICRO
dnl Ex : 1.0.0 => 0
dnl      1.0.3 => 3
dnl      1.1.0 => 100
dnl      1.2.5 => 205
dnl      1.10.9 (who knows) => 1009
dnl
dnl sets GST_LT_LDFLAGS
AS_LIBTOOL(GST, 405, 0, 405)

dnl *** required versions of GStreamer stuff ***
GST_REQ=1.4.0

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl set up gettext
dnl the version check needs to stay here because autopoint greps for it
AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])
AG_GST_GETTEXT([gst-plugins-base-$GST_API_VERSION])

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

dnl let distro override plugin install helper path
AC_ARG_WITH(install-plugins-helper,
  AS_HELP_STRING([--with-install-plugins-helper],[specify path of helper script to call to install plugins]),
  [
    case "${withval}" in
      yes) AC_MSG_ERROR(bad value ${withval} for --with-install-plugins-helper) ;;
      no)  AC_MSG_ERROR(bad value ${withval} for --with-install-plugins-helper) ;;
      *)   GST_INSTALL_PLUGINS_HELPER="${withval}" ;;
    esac
  ],
  [
    dnl Default value
    AS_AC_EXPAND(GST_INSTALL_PLUGINS_HELPER,${libexecdir}/gst-install-plugins-helper)
  ]
)
AC_MSG_NOTICE(Using $GST_INSTALL_PLUGINS_HELPER as plugin install helper)
AC_DEFINE_UNQUOTED(GST_INSTALL_PLUGINS_HELPER, "$GST_INSTALL_PLUGINS_HELPER",
    [plugin install helper script])
AC_SUBST(GST_INSTALL_PLUGINS_HELPER)

AG_GST_ARG_WITH_PLUGINS

AG_GST_ARG_ENABLE_EXTERNAL

AG_GST_ARG_ENABLE_EXPERIMENTAL

dnl *** checks for platform ***
AG_GST_PLATFORM

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

dnl determine if c++ is available on this system
AC_PROG_CXX
dnl CXX may be set to some default even if no c++ compiler is available
dnl (thanks autotools!), so just try to compile some c++ code to make sure
AC_LANG_PUSH([C++])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ class Foo { int bar; };]], [[]])],[working_cxx=yes],[working_cxx=no])
AC_LANG_POP([C++])
AC_MSG_NOTICE([working c++ compiler found: $working_cxx])
AM_CONDITIONAL(HAVE_CXX, test "x$working_cxx" = "xyes")

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for gobject-introspection
GOBJECT_INTROSPECTION_CHECK([1.31.1])

dnl check for documentation tools
GTK_DOC_CHECK([1.12])
AG_GST_PLUGIN_DOCS([1.12])

dnl *** checks for libraries ***

dnl libm, for sin() etc.
LT_LIB_M
AC_SUBST(LIBM)

dnl *** checks for header files ***

dnl check if we have ANSI C header files
AC_HEADER_STDC

dnl check for GCC specific SSE headers
dnl these are used by the speex resampler code
AC_CHECK_HEADERS([xmmintrin.h emmintrin.h])

dnl used in gst/tcp
AC_CHECK_HEADERS([sys/socket.h],
  [HAVE_SYS_SOCKET_H="yes"], [HAVE_SYS_SOCKET_H="no"], [AC_INCLUDES_DEFAULT])
AM_CONDITIONAL(HAVE_SYS_SOCKET_H, test "x$HAVE_SYS_SOCKET_H" = "xyes")

dnl used in gst-libs/gst/rtsp
AC_CHECK_HEADERS([winsock2.h], [HAVE_WINSOCK2_H=yes], [HAVE_WINSOCK2_H=no], [AC_INCLUDES_DEFAULT])
AM_CONDITIONAL(HAVE_WINSOCK2_H, test "x$HAVE_WINSOCK2_H" = "xyes")
if test "x$HAVE_WINSOCK2_H" = "xyes"; then
  WIN32_LIBS="-lws2_32"
  AC_SUBST(WIN32_LIBS)
fi

dnl used in gst-libs/gst/pbutils and associated unit test
AC_CHECK_HEADERS([process.h sys/types.h sys/wait.h sys/stat.h], [], [], [AC_INCLUDES_DEFAULT])

dnl checks for ARM NEON support
dnl this instruction set is used by the speex resampler code
AC_MSG_CHECKING(for ARM NEON support in current arch/CFLAGS)
AC_LINK_IFELSE([
AC_LANG_PROGRAM([[
  #include <arm_neon.h>
  int32x4_t testfunc(int16_t *a, int16_t *b) {
      asm volatile ("vmull.s16 q0, d0, d0" : : : "q0");
      return vmull_s16(vld1_s16(a), vld1_s16(b));
  }
]])],
[
  AC_DEFINE(HAVE_ARM_NEON,[],[ARM NEON support is enabled])
  AC_MSG_RESULT(yes)
],
[
  AC_MSG_RESULT(no)
])

dnl also, Windows does not have long long
AX_CREATE_STDINT_H

dnl *** checks for functions ***
AC_CHECK_FUNCS([localtime_r gmtime_r])

dnl *** checks for math functions ***
LIBS_SAVE=$LIBS
LIBS="$LIBS $LIBM"
AC_CHECK_FUNCS(log2)
LIBS=$LIBS_SAVE

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***

dnl check for fseeko()
AC_FUNC_FSEEKO

dnl Check for fast float to int casting as defined in C99
dnl used to be used in gst-libs/gst/floatcast/floatcast.h (FIXME: still needed?)
save_libs=$LIBS
LIBS="$LIBS $LIBM"
AC_C99_FUNC_LRINT
AC_C99_FUNC_LRINTF
LIBS=$save_libs

dnl Check for a way to display the function name in debug output
AG_GST_CHECK_FUNCTION

dnl *** checks for dependency libraries ***

dnl GLib
GLIB_REQ=2.32
AG_GST_GLIB_CHECK([$GLIB_REQ])

ORC_CHECK([0.4.18])

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_BASE($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_CONTROLLER($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_CHECK($GST_API_VERSION, [$GST_REQ], no)
AM_CONDITIONAL(HAVE_GST_CHECK, test "x$HAVE_GST_CHECK" = "xyes")

dnl Check for documentation xrefs
GLIB_PREFIX="`$PKG_CONFIG --variable=prefix glib-2.0`"
GST_PREFIX="`$PKG_CONFIG --variable=prefix gstreamer-$GST_API_VERSION`"
AC_SUBST(GLIB_PREFIX)
AC_SUBST(GST_PREFIX)

dnl GTK is optional and only used in examples
HAVE_GTK=no
HAVE_GTK_X11=no
GTK_REQ=3.0.0
if test "x$BUILD_EXAMPLES" = "xyes"; then
  PKG_CHECK_MODULES(GTK, gtk+-3.0 >= $GTK_REQ, HAVE_GTK=yes, HAVE_GTK=no)
  dnl some examples need gtk+-x11
  PKG_CHECK_MODULES(GTK_X11, gtk+-x11-3.0 >= $GTK_REQ, HAVE_GTK_X11=yes, HAVE_GTK_X11=no)
  PKG_CHECK_MODULES(GTK_QUARTZ, gtk+-quartz-3.0 >= $GTK_REQ, HAVE_GTK_QUARTZ=yes, HAVE_GTK_QUARTZ=no)
  AC_SUBST(GTK_LIBS)
  AC_SUBST(GTK_CFLAGS)
fi
AM_CONDITIONAL(HAVE_GTK, test "x$HAVE_GTK" = "xyes")
AM_CONDITIONAL(HAVE_GTK_X11, test "x$HAVE_GTK_X11" = "xyes")
AM_CONDITIONAL(HAVE_GTK_QUARTZ, test "x$HAVE_GTK_QUARTZ" = "xyes")

HAVE_QT=no
HAVE_QT_MOC=no
dnl QT is optional and only used in examples
PKG_CHECK_MODULES(QT, Qt5Gui Qt5Widgets, [
  HAVE_QT=yes
], [
  HAVE_QT=no
])
if test "x$HAVE_QT" = "xyes"; then
  AC_CHECK_TOOL(QT_MOC, moc)
  AC_MSG_CHECKING([for Qt5 moc])
  mocversion=`$QT_MOC -v 2>&1`
  mocversiongrep=`echo $mocversion | grep "Qt 5\|moc 5"`
  if test x"$mocversiongrep" != x"$mocversion"; then
    AC_MSG_RESULT([no])
    # moc was not the qt5 one, try with moc-qt5
    AC_CHECK_TOOL(QT_MOC, moc-qt5)
    AC_MSG_CHECKING([for Qt5 moc-qt5])
    mocversion=`$QT_MOC -v 2>&1`
    mocversiongrep=`echo $mocversion | grep "Qt 5\|moc 5"`
    if test x"$mocversiongrep" != x"$mocversion"; then
      AC_CHECK_TOOL(QTCHOOSER, qtchooser)
      AC_MSG_CHECKING([for qtchooser])
      qt5tooldir=`QT_SELECT=qt5 qtchooser -print-env | grep QTTOOLDIR | cut -d '=' -f 2 | cut -d \" -f 2`
      mocversion=`$qt5tooldir/moc -v 2>&1`
      mocversiongrep=`echo $mocversion | grep "Qt 5\|moc 5"`
      if test x"$mocversiongrep" != x"$mocversion"; then
        # no valid moc found
        QT_MOC="no"
      else
        QT_MOC=$qt5tooldir/moc
      fi
    else
      QT_MOC=$QT_MOC
    fi
  fi
  AC_SUBST(QT_MOC)
  AC_MSG_RESULT([$QT_MOC])
fi
AM_CONDITIONAL(HAVE_QT, test "x$HAVE_QT" = "xyes")
AM_CONDITIONAL(HAVE_QT_MOC, test "x$HAVE_QT_MOC" != "xno")

dnl chck for linux headers needed by the joystick seek example
AC_COMPILE_IFELSE(
[
  AC_LANG_PROGRAM([
    #include <linux/input.h>
    #include <linux/joystick.h>
  ],[
    struct js_event js;
    js.type = JS_EVENT_AXIS;
  ])
], [
  HAVE_LINUX_JOYSTICK_HEADERS="yes"
], [
  HAVE_LINUX_JOYSTICK_HEADERS="no"
])
AM_CONDITIONAL(HAVE_LINUX_JOYSTICK_HEADERS, test "x$HAVE_LINUX_JOYSTICK_HEADERS" = "xyes")

dnl Check for -Bsymbolic-functions linker flag used to avoid
dnl intra-library PLT jumps, if available.
AC_ARG_ENABLE(Bsymbolic,
              [AS_HELP_STRING([--disable-Bsymbolic],[avoid linking with -Bsymbolic])],,
              [SAVED_LDFLAGS="${LDFLAGS}"
               AC_MSG_CHECKING([for -Bsymbolic-functions linker flag])
               LDFLAGS=-Wl,-Bsymbolic-functions
               AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[int main (void) { return 0; }]])],[
                           AC_MSG_RESULT(yes)
                           enable_Bsymbolic=yes],[
                           AC_MSG_RESULT(no)
                           enable_Bsymbolic=no])
               LDFLAGS="${SAVED_LDFLAGS}"])


dnl *** set variables based on configure arguments ***

dnl set license and copyright notice
GST_LICENSE="LGPL"
AC_DEFINE_UNQUOTED(GST_LICENSE, "$GST_LICENSE", [GStreamer license])
AC_SUBST(GST_LICENSE)

dnl set location of plugin directory
AG_GST_SET_PLUGINDIR

dnl set release date/time
AG_GST_SET_PACKAGE_RELEASE_DATETIME_WITH_NANO([$PACKAGE_VERSION_NANO],
  ["${srcdir}/gst-plugins-base.doap"],
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

# set by AG_GST_PARSE_SUBSYSTEM_DISABLES above
dnl make sure it doesn't complain about unused variables if debugging is disabled
NO_WARNINGS=""
AG_GST_CHECK_GST_DEBUG_DISABLED([NO_WARNINGS="-Wno-unused"], [NO_WARNINGS=""])

dnl define an ERROR_CFLAGS Makefile variable
dnl -Wformat-nonliteral - see ext/pango/gstclockoverlay.c and http://gcc.gnu.org/bugzilla/show_bug.cgi?id=39438
AG_GST_SET_ERROR_CFLAGS($FATAL_WARNINGS, [
    -Wmissing-declarations -Wmissing-prototypes -Wredundant-decls -Wundef
    -Wwrite-strings -Wformat-nonliteral -Wformat-security
    -Winit-self -Wmissing-include-dirs -Waddress -Waggregate-return
    -Wno-multichar -Wnested-externs $NO_WARNINGS])

dnl define an ERROR_CXXFLAGS Makefile variable
AG_GST_SET_ERROR_CXXFLAGS($FATAL_WARNINGS, [
    -Wmissing-declarations -Wredundant-decls -Wundef
    -Wwrite-strings -Wformat-nonliteral -Wformat-security
    -Winit-self -Wmissing-include-dirs -Waddress -Waggregate-return
    -Wno-multichar $NO_WARNINGS])

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT($GST_GIT)

dnl used in examples
AG_GST_DEFAULT_ELEMENTS

dnl needed for encoding-target
GST_DATADIR="$GST_PREFIX/share"
AC_DEFINE_UNQUOTED(GST_DATADIR, "$GST_DATADIR", [system wide data directory])
AC_DEFINE_UNQUOTED(GST_API_VERSION, "$GST_API_VERSION", [major/minor version])

dnl behaviour of speex based audio resampler
AC_MSG_CHECKING(which audio resample format to use for integer)
AC_ARG_WITH([audioresample_format],
        AS_HELP_STRING([--with-audioresample-format],[Which implementation should be used for integer audio resampling, int/float/auto, (default is auto)]),
        [ac_cv_audioresample_format=$withval], [ac_cv_audioresample_format=auto])dnl
AC_MSG_RESULT($ac_cv_audioresample_format)
case $ac_cv_audioresample_format in
  int)
    AC_DEFINE(AUDIORESAMPLE_FORMAT_INT,1,[The int implementation should be used for integer audio resampling])
    AC_SUBST(AUDIORESAMPLE_FORMAT_INT)
  ;;
  float)
    AC_DEFINE(AUDIORESAMPLE_FORMAT_FLOAT,1,[The float implementation should be used for integer audio resampling])
    AC_SUBST(AUDIORESAMPLE_FORMAT_FLOAT)
  ;;
  auto)
    AC_DEFINE(AUDIORESAMPLE_FORMAT_AUTO,1,[The implementation that should be used for integer audio resampling witll be benchmarked at runtime])
    AC_SUBST(AUDIORESAMPLE_FORMAT_AUTO)
esac

dnl Check for mmap (needed by allocators library)
AC_CHECK_FUNC([mmap], [AC_DEFINE(HAVE_MMAP, 1, [Defined if mmap is supported])])

dnl *** plug-ins to include ***

dnl these are all the gst plug-ins, compilable without additional libs
AG_GST_CHECK_PLUGIN(adder)
AG_GST_CHECK_PLUGIN(app)
AG_GST_CHECK_PLUGIN(audioconvert)
AG_GST_CHECK_PLUGIN(audiorate)
AG_GST_CHECK_PLUGIN(audiotestsrc)
AG_GST_CHECK_PLUGIN(encoding)
AG_GST_CHECK_PLUGIN(videoconvert)
AG_GST_CHECK_PLUGIN(gio)
AG_GST_CHECK_PLUGIN(playback)
AG_GST_CHECK_PLUGIN(audioresample)
AG_GST_CHECK_PLUGIN(subparse)
AG_GST_CHECK_PLUGIN(tcp)
AG_GST_CHECK_PLUGIN(typefind)
AG_GST_CHECK_PLUGIN(videotestsrc)
AG_GST_CHECK_PLUGIN(videorate)
AG_GST_CHECK_PLUGIN(videoscale)
AG_GST_CHECK_PLUGIN(volume)

dnl iso-codes is optional, used by libgsttag
AC_ARG_ENABLE(iso-codes,
AS_HELP_STRING([--enable-iso-codes],[use iso-codes if installed]),
[case "${enableval}" in
  yes) enable_iso_codes=yes ;;
  no)  enable_iso_codes=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-iso-codes) ;;
esac
],
[enable_iso_codes=yes]) dnl Default value

AC_MSG_CHECKING([whether to use iso-codes if they are available])
if test "x$enable_iso_codes" = "xyes"; then
  AC_MSG_RESULT([yes])
  have_iso_codes=no
  AC_MSG_CHECKING([whether iso-codes are available on this system])
  if $PKG_CONFIG iso-codes; then
    AC_MSG_RESULT([yes])
    AC_MSG_CHECKING([whether iso-codes has iso-639 domain])
    if $PKG_CONFIG --variable=domains iso-codes | $GREP -q 639 ; then
      AC_MSG_RESULT([yes])
      AC_MSG_CHECKING([for iso-codes prefix])
      ISO_CODES_PREFIX=`$PKG_CONFIG --variable=prefix iso-codes`
      if test -d "$ISO_CODES_PREFIX"; then
        AC_MSG_RESULT([yes])
        AC_MSG_CHECKING([ISO_CODES_PREFIX])
        AC_MSG_RESULT([$ISO_CODES_PREFIX])
        ISO_639_DOMAIN="iso_639"
        AC_MSG_CHECKING([ISO_639_DOMAIN])
        AC_MSG_RESULT([$ISO_639_DOMAIN])
        have_iso_codes=yes
        AC_DEFINE([HAVE_ISO_CODES], [1], [make use of iso-codes for ISO-639])
        AC_DEFINE_UNQUOTED([ISO_CODES_PREFIX], ["$ISO_CODES_PREFIX"], [prefix])
        ISO_CODES_VERSION=`$PKG_CONFIG --modversion iso-codes`
        AC_DEFINE_UNQUOTED([ISO_CODES_VERSION], ["$ISO_CODES_VERSION"], [ ])
      else
        AC_MSG_RESULT([no])
      fi
    else
      AC_MSG_RESULT([no])
    fi
  else
    AC_MSG_RESULT([no])
  fi
  AM_CONDITIONAL(USE_ISO_CODES, test "x$have_iso_codes" = "xyes")
else
  AC_MSG_RESULT([no (disabled via --disable-iso-codes)])
  AM_CONDITIONAL(USE_ISO_CODES, false)
fi

dnl *** zlib is optionally used by id3 tag parsing in libgsttag ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ZLIB, true)
AG_GST_CHECK_FEATURE(ZLIB, [zlib support for ID3 parsing in libgsttag],, [
  PKG_CHECK_MODULES(ZLIB, [ zlib ], [
      HAVE_ZLIB="yes"
    ], [
      AG_GST_CHECK_LIBHEADER(ZLIB, z, uncompress,, zlib.h, [
        HAVE_ZLIB="yes"
        ZLIB_LIBS="-lz"
        AC_SUBST(ZLIB_LIBS)
      ])
    ])
])

dnl *** sys plug-ins ***

echo
AC_MSG_NOTICE([Checking libraries for plugins in sys/])
echo

dnl *** X11 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_X, true)
AG_GST_CHECK_FEATURE(X, [X libraries and plugins],
                  [ximagesink], [
  AG_GST_CHECK_X
])

dnl Check for Xv extension
translit(dnm, m, l) AM_CONDITIONAL(USE_XVIDEO, true)
AG_GST_CHECK_FEATURE(XVIDEO, [X11 XVideo extensions],
                  [xvimagesink], [
  AG_GST_CHECK_XV
])

dnl check for X Shm
translit(dnm, m, l) AM_CONDITIONAL(USE_XSHM, true)
AG_GST_CHECK_FEATURE(XSHM, [X Shared Memory extension], , [
  if test x$HAVE_X = xyes; then
    AC_CHECK_LIB(Xext, XShmAttach,
		 HAVE_XSHM="yes", HAVE_XSHM="no",
		 $X_LIBS)
    if test "x$HAVE_XSHM" = "xyes"; then
      XSHM_LIBS="-lXext"
    else
      dnl On AIX, it is in XextSam instead, but we still need -lXext
      AC_CHECK_LIB(XextSam, XShmAttach,
		   HAVE_XSHM="yes", HAVE_XSHM="no",
		   $X_LIBS)
      if test "x$HAVE_XSHM" = "xyes"; then
	XSHM_LIBS="-lXext -lXextSam"
      fi
    fi
  fi
], , [
  AC_SUBST(HAVE_XSHM)
  AC_SUBST(XSHM_LIBS)
])

dnl *** ext plug-ins ***
dnl keep this list sorted alphabetically !

if test "x$BUILD_EXTERNAL" = "xyes"; then

echo
AC_MSG_NOTICE([Checking libraries for plugins in ext/])
echo

dnl *** alsa ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ALSA, true)
AG_GST_CHECK_FEATURE(ALSA, [ALSA], alsa, [
  PKG_CHECK_MODULES(ALSA, alsa >= 0.9.1, [
    HAVE_ALSA="yes"
    AC_SUBST(ALSA_CFLAGS)
    AC_SUBST(ALSA_LIBS)
  ], [
    AM_PATH_ALSA(0.9.1, HAVE_ALSA="yes", HAVE_ALSA="no")
  ])
])

dnl *** CDParanoia ***
dnl Note: upstream has a pkg-config file only in post-10.2 SVN so far
translit(dnm, m, l) AM_CONDITIONAL(USE_CDPARANOIA, true)
AG_GST_CHECK_FEATURE(CDPARANOIA, [CDParanoia], cdparanoia, [
  PKG_CHECK_MODULES(CDPARANOIA, cdparanoia-3 >= 10.2, [
    HAVE_CDPARANOIA="yes"
  ], [
    AG_GST_CHECK_LIBHEADER(CDPARANOIA, cdda_interface,
                        cdda_open, -lm,
                        cdda_interface.h,
                        CDPARANOIA_LIBS="-lcdda_interface -lcdda_paranoia"
                        HEADER_DIR="no"
                        FOUND_CDPARANOIA="yes")
    if test "x$FOUND_CDPARANOIA" != "xyes";
    then
      AG_GST_CHECK_LIBHEADER(CDPARANOIA, cdda_interface,
                          cdda_open, -lm,
                          cdda/cdda_interface.h,
                          CDPARANOIA_LIBS="-lcdda_interface -lcdda_paranoia"
                          HEADER_DIR="yes"
                          FOUND_CDPARANOIA="yes")
    fi
    if test "x$HEADER_DIR" = "xyes";
    then
      AC_DEFINE_UNQUOTED(CDPARANOIA_HEADERS_IN_DIR, ,
                         defined if cdda headers are in a cdda/ directory)
    fi
    if test "x$FOUND_CDPARANOIA" = "xyes";
    then
      AC_CHECK_LIB(cdda_paranoia,
                   paranoia_cachemodel_size,
                   : ,
                   HAVE_CDPARANOIA=no,
  		 [-lcdda_interface])
    fi

    AC_SUBST(CDPARANOIA_LIBS)
  ])
])
dnl FIXME : add second check somehow if that is necessary
dnl AC_CHECK_LIB(cdda_paranoia, paranoia_init, : , HAVE_CDPARANOIA=no, -lcdda_interface )
dnl AC_CHECK_HEADER(cdda_paranoia.h, :, HAVE_CDPARANOIA=no)

dnl *** ivorbis ***
dnl AM_PATH_IVORBIS only takes two options
translit(dnm, m, l) AM_CONDITIONAL(USE_IVORBIS, true)
AG_GST_CHECK_FEATURE(IVORBIS, [integer vorbis plug-in], ivorbisdec, [
  AG_GST_PKG_CHECK_MODULES(IVORBIS, vorbisidec)
  if test $HAVE_IVORBIS = no
  then
    IVORBIS_LIBS=
    IVORBIS_CFLAGS=
    AC_CHECK_LIB(vorbisidec, vorbis_block_init,
      [IVORBIS_LIBS=-lvorbisidec
      HAVE_IVORBIS=yes
      case $host in
      arm-*-*)
        IVORBIS_CFLAGS="-D_ARM_ASSEM_ $IVORBIS_CFLAGS"
      esac
      ],
      HAVE_IVORBIS=no)
    AC_SUBST(IVORBIS_LIBS)
    AC_SUBST(IVORBIS_CFLAGS)
  else
    AC_CHECK_LIB(vorbisidec, vorbis_dsp_pcmout,
      AC_DEFINE([USE_TREMOLO],1, "Define if building for android"),
      USE_TREMOLO=no)
  fi
])

dnl *** libvisual ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBVISUAL, true)
AG_GST_CHECK_FEATURE(LIBVISUAL, [libvisual visualization library], libvisual, [
  AG_GST_PKG_CHECK_MODULES(LIBVISUAL, libvisual-0.4 >= 0.4.0)
  if test x$HAVE_LIBVISUAL = xyes; then
    LIBVIS_PLUGINSDIR="`$PKG_CONFIG --variable=pluginsbasedir libvisual-0.4`"
  fi
  AC_MSG_NOTICE([libvisual pluginsdir: $LIBVIS_PLUGINSDIR])
  if test x$LIBVIS_PLUGINSDIR != x; then
    AC_DEFINE_UNQUOTED(LIBVISUAL_PLUGINSBASEDIR,
        "$LIBVIS_PLUGINSDIR",
        [directory in which the detected libvisual's plugins are located])
  fi
])

dnl *** ogg ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OGG, true)
AG_GST_CHECK_FEATURE(OGG, [Xiph Ogg library], ogg, [
  AG_GST_PKG_CHECK_MODULES(OGG, ogg >= 1.0)
])

dnl *** pango ***
translit(dnm, m, l) AM_CONDITIONAL(USE_PANGO, true)
AG_GST_CHECK_FEATURE(PANGO, [Pango font rendering], pango, [
  AG_GST_PKG_CHECK_MODULES(PANGO, pango >= 1.22.0  pangocairo >= 1.22.0)
])

dnl *** theora ***
translit(dnm, m, l) AM_CONDITIONAL(USE_THEORA, true)
AG_GST_CHECK_FEATURE(THEORA, [Xiph Theora video codec], theora, [
  AG_GST_PKG_CHECK_MODULES(THEORA, theoradec >= 1.1 theoraenc >= 1.1)
])

dnl *** vorbis ***
translit(dnm, m, l) AM_CONDITIONAL(USE_VORBIS, true)
AG_GST_CHECK_FEATURE(VORBIS, [Xiph Vorbis audio codec], vorbis, [
  AG_GST_PKG_CHECK_MODULES(VORBIS, vorbis >= 1.0 vorbisenc >= 1.0)
])

if test "x$HAVE_VORBIS" = "xyes"; then
  ac_cflags_save="$CFLAGS"
  AC_COMPILE_IFELSE([
    AC_LANG_PROGRAM([[
#include <vorbis/codec.h>
                     ]],[[
vorbis_dsp_state *v;

vorbis_synthesis_restart (v);
                     ]])], HAVE_VSR=yes, HAVE_VSR=no)
  if test "x$HAVE_VSR" = "xyes"; then
    AC_DEFINE_UNQUOTED(HAVE_VORBIS_SYNTHESIS_RESTART, 1,
                       [defined if vorbis_synthesis_restart is present])
  fi
  CFLAGS="$ac_cflags_save"
fi

else

dnl not building plugins with external dependencies,
dnl but we still need to set the conditionals
AM_CONDITIONAL(USE_ALSA, false)
AM_CONDITIONAL(USE_CDPARANOIA, false)
AM_CONDITIONAL(USE_IVORBIS, false)
AM_CONDITIONAL(USE_LIBVISUAL, false)
AM_CONDITIONAL(USE_OGG, false)
AM_CONDITIONAL(USE_PANGO, false)
AM_CONDITIONAL(USE_THEORA, false)
AM_CONDITIONAL(USE_VORBIS, false)

fi dnl of EXT plugins

dnl seeking needs freetype, so check for it here
PKG_CHECK_MODULES(FT2, freetype2 >= 2.0.9, HAVE_FT2="yes", [
  AC_CHECK_FT2(2.0.9, HAVE_FT2="yes", HAVE_FT2="no")
])
dnl make the HAVE_FT2 variable available to automake and Makefile.am
AM_CONDITIONAL(HAVE_FT2, test "x$HAVE_FT2" = "xyes")
AC_SUBST(FT2_CFLAGS)
AC_SUBST(FT2_LIBS)

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
GST_OPTION_CXXFLAGS="\$(WARNING_CXXFLAGS) \$(ERROR_CXXFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
AC_SUBST(GST_OPTION_CFLAGS)
AC_SUBST(GST_OPTION_CXXFLAGS)

dnl our libraries need to be versioned correctly
AC_SUBST(GST_LT_LDFLAGS)

dnl GST_PLUGINS_BASE_CFLAGS
dnl prefer internal headers to already installed ones
dnl also add builddir include for enumtypes and marshal
GST_PLUGINS_BASE_CFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs"
AC_SUBST(GST_PLUGINS_BASE_CFLAGS)

dnl FIXME: do we want to rename to GST_ALL_* ?
dnl add GST_OPTION_CFLAGS, but overridable
GST_CFLAGS="$GST_CFLAGS"
GST_CXXFLAGS="$GLIB_CFLAGS $GST_CFLAGS \$(GLIB_EXTRA_CFLAGS) \$(GST_OPTION_CXXFLAGS)"
GST_CFLAGS="$GLIB_CFLAGS $GST_CFLAGS \$(GLIB_EXTRA_CFLAGS) \$(GST_OPTION_CFLAGS)"
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_CXXFLAGS)
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

dnl GST_LIB_LDFLAGS
dnl linker flags shared by all libraries
dnl LDFLAGS modifier defining exported symbols from built libraries
dnl (export _gst_foo but not __gst_foo)
GST_LIB_LDFLAGS="-export-symbols-regex ^_?\(gst_\|Gst\|GST_\).*"
AC_SUBST(GST_LIB_LDFLAGS)

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_.*' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl *** output files ***

$MKDIR_P tests/check/orc

AC_CONFIG_FILES(
Makefile
gst-plugins-base.spec
gst/Makefile
gst/adder/Makefile
gst/app/Makefile
gst/audioconvert/Makefile
gst/audiorate/Makefile
gst/audiotestsrc/Makefile
gst/encoding/Makefile
gst/videoconvert/Makefile
gst/gio/Makefile
gst/playback/Makefile
gst/audioresample/Makefile
gst/subparse/Makefile
gst/tcp/Makefile
gst/typefind/Makefile
gst/videotestsrc/Makefile
gst/videorate/Makefile
gst/videoscale/Makefile
gst/volume/Makefile
sys/Makefile
sys/ximage/Makefile
sys/xvimage/Makefile
ext/Makefile
ext/alsa/Makefile
ext/cdparanoia/Makefile
ext/libvisual/Makefile
ext/ogg/Makefile
ext/pango/Makefile
ext/theora/Makefile
ext/vorbis/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/allocators/Makefile
gst-libs/gst/audio/Makefile
gst-libs/gst/app/Makefile
gst-libs/gst/fft/Makefile
gst-libs/gst/riff/Makefile
gst-libs/gst/rtp/Makefile
gst-libs/gst/rtsp/Makefile
gst-libs/gst/sdp/Makefile
gst-libs/gst/tag/Makefile
gst-libs/gst/pbutils/Makefile
gst-libs/gst/pbutils/gstpluginsbaseversion.h
gst-libs/gst/video/Makefile
tools/Makefile
pkgconfig/Makefile
pkgconfig/gstreamer-allocators.pc
pkgconfig/gstreamer-allocators-uninstalled.pc
pkgconfig/gstreamer-audio.pc
pkgconfig/gstreamer-audio-uninstalled.pc
pkgconfig/gstreamer-app.pc
pkgconfig/gstreamer-app-uninstalled.pc
pkgconfig/gstreamer-fft.pc
pkgconfig/gstreamer-fft-uninstalled.pc
pkgconfig/gstreamer-pbutils.pc
pkgconfig/gstreamer-pbutils-uninstalled.pc
pkgconfig/gstreamer-riff.pc
pkgconfig/gstreamer-riff-uninstalled.pc
pkgconfig/gstreamer-rtp.pc
pkgconfig/gstreamer-rtp-uninstalled.pc
pkgconfig/gstreamer-rtsp.pc
pkgconfig/gstreamer-rtsp-uninstalled.pc
pkgconfig/gstreamer-sdp.pc
pkgconfig/gstreamer-sdp-uninstalled.pc
pkgconfig/gstreamer-tag.pc
pkgconfig/gstreamer-tag-uninstalled.pc
pkgconfig/gstreamer-video.pc
pkgconfig/gstreamer-video-uninstalled.pc
pkgconfig/gstreamer-plugins-base.pc
pkgconfig/gstreamer-plugins-base-uninstalled.pc
tests/Makefile
tests/check/Makefile
tests/examples/Makefile
tests/examples/app/Makefile
tests/examples/audio/Makefile
tests/examples/dynamic/Makefile
tests/examples/encoding/Makefile
tests/examples/fft/Makefile
tests/examples/gio/Makefile
tests/examples/overlay/Makefile
tests/examples/seek/Makefile
tests/examples/snapshot/Makefile
tests/examples/playback/Makefile
tests/examples/playrec/Makefile
tests/files/Makefile
tests/icles/Makefile
tests/icles/playback/Makefile
docs/Makefile
docs/design/Makefile
docs/libs/Makefile
docs/plugins/Makefile
docs/version.entities
po/Makefile.in
common/Makefile
common/m4/Makefile
m4/Makefile
)

dnl Create the config.h file for Visual Studio builds
dnl Beware of spaces and /'s in some of the shell variable contents.
sed \
    -e 's/.*config.h.in.*autoheader.*/\/* Autogenerated config.h created for win32 Visual Studio builds *\/\n\n\/* PREFIX -- specifically added for Windows for easier moving *\/\n#define PREFIX "C:\\\\gstreamer"/' \
    -e 's/.* GST_INSTALL_PLUGINS_HELPER/#define GST_INSTALL_PLUGINS_HELPER PREFIX "\\\\libexec\\\\gst-install-plugins-helper.exe"/' \
    -e 's/.* GETTEXT_PACKAGE$/#define GETTEXT_PACKAGE "'$GETTEXT_PACKAGE'"/' \
    -e 's/.* GST_DATADIR$/#define GST_DATADIR PREFIX "\\\\share"/' \
    -e 's/.* GST_LEVEL_DEFAULT$/#define GST_LEVEL_DEFAULT GST_LEVEL_ERROR/' \
    -e 's/.* GST_LICENSE$/#define GST_LICENSE "'$GST_LICENSE'"/' \
    -e 's/.* GST_API_VERSION$/#define GST_API_VERSION "'$GST_API_VERSION'"/' \
    -e "s,.* GST_PACKAGE_NAME$,#define GST_PACKAGE_NAME \"${GST_PACKAGE_NAME}\"," \
    -e 's/.* GST_PACKAGE_ORIGIN$/#define GST_PACKAGE_ORIGIN "Unknown package origin"/' \
    -e "s,.* GST_PACKAGE_RELEASE_DATETIME$,#define GST_PACKAGE_RELEASE_DATETIME \"${GST_PACKAGE_RELEASE_DATETIME}\"," \
    -e 's/.* HAVE_CPU_I386$/#define HAVE_CPU_I386 1/' \
    -e 's/.* HAVE_FGETPOS$/#define HAVE_FGETPOS 1/' \
    -e 's/.* HAVE_FSETPOS$/#define HAVE_FSETPOS 1/' \
    -e 's/.* HAVE_ISO_CODES$/#undef HAVE_ISO_CODES/' \
    -e 's/.* HAVE_PROCESS_H$/#define HAVE_PROCESS_H 1/' \
    -e 's/.* HAVE_STDLIB_H$/#define HAVE_STDLIB_H 1/' \
    -e 's/.* HAVE_STRING_H$/#define HAVE_STRING_H 1/' \
    -e 's/.* HAVE_SYS_STAT_H$/#define HAVE_SYS_STAT_H 1/' \
    -e 's/.* HAVE_SYS_TYPES_H$/#define HAVE_SYS_TYPES_H 1/' \
    -e 's/.* HAVE_WIN32$/#define HAVE_WIN32 1/' \
    -e 's/.* HOST_CPU$/#define HOST_CPU "i686"/' \
    -e 's/.* ISO_CODES_PREFIX$/#undef ISO_CODES_PREFIX/' \
    -e 's/.* ISO_CODES_VERSION$/#undef ISO_CODES_VERSION/' \
    -e 's/.* LIBDIR$/#ifdef _DEBUG\n#  define LIBDIR PREFIX "\\\\debug\\\\lib"\n#else\n#  define LIBDIR PREFIX "\\\\lib"\n#endif/' \
    -e 's/.* LOCALEDIR$/#define LOCALEDIR PREFIX "\\\\share\\\\locale"/' \
    -e "s/.* PACKAGE$/#define PACKAGE \"$PACKAGE\"/" \
    -e 's/.* PACKAGE_BUGREPORT$/#define PACKAGE_BUGREPORT "http:\/\/bugzilla.gnome.org\/enter_bug.cgi?product=GStreamer"/' \
    -e "s/.* PACKAGE_NAME$/#define PACKAGE_NAME \"$PACKAGE_NAME\"/" \
    -e "s/.* PACKAGE_STRING$/#define PACKAGE_STRING \"$PACKAGE_STRING\"/" \
    -e 's/.* PACKAGE_TARNAME$/#define PACKAGE_TARNAME "'$PACKAGE_TARNAME'"/' \
    -e 's/.* PACKAGE_VERSION$/#define PACKAGE_VERSION "'$PACKAGE_VERSION'"/' \
    -e 's/.* PLUGINDIR$/#ifdef _DEBUG\n#  define PLUGINDIR PREFIX "\\\\debug\\\\lib\\\\gstreamer-0.11"\n#else\n#  define PLUGINDIR PREFIX "\\\\lib\\\\gstreamer-0.11"\n#endif/' \
    -e 's/.* USE_BINARY_REGISTRY$/#define USE_BINARY_REGISTRY/' \
    -e 's/.* VERSION$/#define VERSION "'$VERSION'"/' \
    -e "s/.* DEFAULT_AUDIOSINK$/#define DEFAULT_AUDIOSINK \"directsoundsink\"/" \
    -e "s/.* DEFAULT_VIDEOSINK$/#define DEFAULT_VIDEOSINK \"directdrawsink\"/" \
    -e "s/.* DEFAULT_VISUALIZER$/#define DEFAULT_VISUALIZER \"goom\"/" \
    config.h.in >win32/common/config.h-new



AC_OUTPUT

AG_GST_OUTPUT_PLUGINS
ORC_OUTPUT

