AC_PREREQ(2.60)

dnl please read gstreamer/docs/random/autotools before changing this file

dnl initialize autoconf
dnl releases only do -Wall, git and prerelease does -Werror too
dnl use a three digit version number for releases, and four for git/prerelease
AC_INIT(GStreamer Base Plug-ins, 0.10.30,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gst-plugins-base)

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability 1.10])

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_GIT="no", GST_GIT="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/audiotestsrc/gstaudiotestsrc.c])

dnl define the output header for config
AM_CONFIG_HEADER([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl sets host_* variables
AC_CANONICAL_HOST

dnl use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST(AM_DEFAULT_VERBOSITY)])

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
dnl sets GST_LT_LDFLAGS
AS_LIBTOOL(GST, 21, 0, 21)

dnl FIXME: this macro doesn't actually work;
dnl the generated libtool script has no support for the listed tags.
dnl So this needs to be fixed first if we want to use this
dnl AS_LIBTOOL_TAGS

AC_LIBTOOL_WIN32_DLL
AM_PROG_LIBTOOL

dnl *** required versions of GStreamer stuff ***
GST_REQ=0.10.30

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl set up gettext
dnl the version check needs to stay here because autopoint greps for it
AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])
AG_GST_GETTEXT([gst-plugins-base-$GST_MAJORMINOR])

dnl *** check for arguments to configure ***

AG_GST_ARG_DEBUG
AG_GST_ARG_PROFILING
AG_GST_ARG_VALGRIND
AG_GST_ARG_GCOV

AG_GST_ARG_EXAMPLES

AG_GST_ARG_WITH_PKG_CONFIG_PATH
AG_GST_ARG_WITH_PACKAGE_NAME
AG_GST_ARG_WITH_PACKAGE_ORIGIN

dnl let distro override plugin install helper path
AC_ARG_WITH(install-plugins-helper,
  AC_HELP_STRING([--with-install-plugins-helper],
    [specify path of helper script to call to install plugins]),
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
AC_PROG_CXX

dnl determine if c++ is available on this system
AC_CHECK_PROG(HAVE_CXX, $CXX, yes, no)

dnl determine c++ preprocessor
dnl FIXME: do we need this ?
AC_PROG_CXXCPP

dnl check if the compiler supports '-c' and '-o' options
AM_PROG_CC_C_O

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for gobject-introspection
GOBJECT_INTROSPECTION_CHECK([0.6.3])

dnl check for documentation tools
GTK_DOC_CHECK([1.3])
AS_PATH_PYTHON([2.1])
AG_GST_PLUGIN_DOCS([1.3],[2.1])

dnl *** checks for libraries ***

dnl libm, for sin() etc.
AC_CHECK_LIBM
AC_SUBST(LIBM)

dnl *** checks for header files ***

dnl check if we have ANSI C header files
AC_HEADER_STDC

dnl used in gst/ffmpegcolorspace/mem.c
dnl FIXME: could be fixed by redefining av_malloc and av_free to GLib's
AC_CHECK_HEADERS([malloc.h])

ac_cppflags_save="$CPPFLAGS"
CPPFLAGS="`$PKG_CONFIG --cflags libxml-2.0`"
AC_COMPILE_IFELSE(
  AC_LANG_PROGRAM([
#include <libxml/HTMLparser.h>
                     ],[
#ifndef LIBXML_HTML_ENABLED
#error libxml2 has no HTML support
#endif /* LIBXML_HTML_ENABLED */
                   ]), HAVE_LIBXML_HTML="yes", HAVE_LIBXML_HTML="no")
CPPFLAGS="$ac_cppflags_save"
AM_CONDITIONAL(HAVE_LIBXML_HTML, test "x$HAVE_LIBXML_HTML" = "xyes")

dnl used in gst/tcp
AC_CHECK_HEADERS([sys/socket.h], 
  HAVE_SYS_SOCKET_H="yes", HAVE_SYS_SOCKET_H="no")
AM_CONDITIONAL(HAVE_SYS_SOCKET_H, test "x$HAVE_SYS_SOCKET_H" = "xyes")

dnl used in gst-libs/gst/rtsp
AC_CHECK_HEADERS([winsock2.h], HAVE_WINSOCK2_H=yes)
if test "x$HAVE_WINSOCK2_H" = "xyes"; then
  WIN32_LIBS="-lws2_32"
  AC_SUBST(WIN32_LIBS)
fi

dnl check for hstrerror, for gst-libs/gst/rtsp
save_libs="$LIBS"
AC_CHECK_FUNC(hstrerror, ,
    AC_CHECK_LIB(resolv, hstrerror, ,
        [AC_DEFINE(NO_HSTRERROR, 1,
             [Define if you have no native hstrerror() function.])]))
HSTRERROR_LIBS=$LIBS
AC_SUBST(HSTRERROR_LIBS)
LIBS="$save_libs"

dnl used in gst-libs/gst/pbutils and associated unit test
AC_CHECK_HEADERS([process.h sys/types.h sys/wait.h sys/stat.h])

dnl ffmpegcolorspace includes _stdint.h
dnl also, Windows does not have long long
AX_CREATE_STDINT_H

dnl *** checks for functions ***
AC_CHECK_FUNCS([localtime_r gmtime_r])

dnl *** checks for math functions ***

dnl *** checks for types/defines ***

dnl Check for FIONREAD ioctl declaration
dnl used in gst/tcp
GST_CHECK_FIONREAD

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***

dnl check for fseeko()
AC_FUNC_FSEEKO

dnl Check for fast float to int casting as defined in C99
dnl used in gst-libs/gst/floatcast/floatcast.h
save_libs=$LIBS
LIBS="$LIBS $LIBM"
AC_C99_FUNC_LRINT
AC_C99_FUNC_LRINTF
LIBS=$save_libs

dnl Check for a way to display the function name in debug output
AG_GST_CHECK_FUNCTION

dnl *** checks for socket and nsl libraries ***
AC_CHECK_FUNC(socket,,[AC_CHECK_LIB(socket,socket)])
AC_CHECK_FUNC(gethostbyname,,[AC_CHECK_LIB(nsl,gethostbyname)])

dnl *** checks for dependency libraries ***

dnl GLib is required
AG_GST_GLIB_CHECK([2.20])

ORC_CHECK([0.4.5])

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_MAJORMINOR, [$GST_REQ], yes)
AG_GST_CHECK_GST_BASE($GST_MAJORMINOR, [$GST_REQ], yes)
AG_GST_CHECK_GST_GDP($GST_MAJORMINOR, [$GST_REQ], yes)
AG_GST_CHECK_GST_CONTROLLER($GST_MAJORMINOR, [$GST_REQ], yes)
AG_GST_CHECK_GST_CHECK($GST_MAJORMINOR, [$GST_REQ], no)
AM_CONDITIONAL(HAVE_GST_CHECK, test "x$HAVE_GST_CHECK" = "xyes")

dnl Check for documentation xrefs
GLIB_PREFIX="`$PKG_CONFIG --variable=prefix glib-2.0`"
GST_PREFIX="`$PKG_CONFIG --variable=prefix gstreamer-$GST_MAJORMINOR`"
AC_SUBST(GLIB_PREFIX)
AC_SUBST(GST_PREFIX)

dnl GTK is optional and only used in examples
PKG_CHECK_MODULES(GTK, gtk+-3.0, HAVE_GTK=yes,
    [PKG_CHECK_MODULES(GTK, gtk+-2.0 >= 2.14.0, HAVE_GTK=yes, HAVE_GTK=no)])
AM_CONDITIONAL(HAVE_GTK, test "x$HAVE_GTK" = "xyes")

dnl some examples need gtk+-x11
PKG_CHECK_MODULES(GTK_X11, gtk+-x11-3.0, HAVE_GTK_X11=yes,
    [PKG_CHECK_MODULES(GTK_X11, gtk+-x11-2.0 >= 2.14.0, HAVE_GTK_X11=yes, HAVE_GTK_X11=no)])
AM_CONDITIONAL(HAVE_GTK_X11, test "x$HAVE_GTK_X11" = "xyes")

dnl QT is optional and only used in examples
PKG_CHECK_MODULES(QT, QtGui >= 4.6, [
  HAVE_QT_GV=yes
  HAVE_QT=yes
], [
  HAVE_QT_GV=no
  PKG_CHECK_MODULES(QT, QtGui >= 4.0, HAVE_QT=yes, HAVE_QT=no)
])
if test "x$HAVE_QT" = "xyes"; then
  AC_MSG_CHECKING([Qt moc])
  QT4_MOC=`$PKG_CONFIG --variable=moc_location QtGui`
  AC_MSG_RESULT($QT4_MOC)
  AC_SUBST(QT4_MOC)
fi
AM_CONDITIONAL(HAVE_QT, test "x$HAVE_QT" = "xyes")
AM_CONDITIONAL(HAVE_QT_GV, test "x$HAVE_QT_GV" = "xyes")

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

dnl *** set variables based on configure arguments ***

dnl set license and copyright notice
GST_LICENSE="LGPL"
AC_DEFINE_UNQUOTED(GST_LICENSE, "$GST_LICENSE", [GStreamer license])
AC_SUBST(GST_LICENSE)

dnl set location of plugin directory
AG_GST_SET_PLUGINDIR

dnl define an ERROR_CFLAGS Makefile variable
dnl -Wformat-nonliteral - see ext/pango/gstclockoverlay.c and http://gcc.gnu.org/bugzilla/show_bug.cgi?id=39438
AG_GST_SET_ERROR_CFLAGS($GST_GIT, [
    -Wmissing-declarations -Wmissing-prototypes -Wredundant-decls -Wundef
    -Wwrite-strings -Wformat-nonliteral -Wformat-security
    -Winit-self -Wmissing-include-dirs -Waddress -Waggregate-return
    -Wno-multichar -Wnested-externs ])

dnl define an ERROR_CXXFLAGS Makefile variable
AG_GST_SET_ERROR_CXXFLAGS($GST_GIT, [
    -Wmissing-declarations -Wredundant-decls -Wundef
    -Wwrite-strings -Wformat-nonliteral -Wformat-security
    -Winit-self -Wmissing-include-dirs -Waddress -Waggregate-return
    -Wno-multichar])

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT($GST_GIT)

dnl used in examples
AG_GST_DEFAULT_ELEMENTS

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

dnl *** plug-ins to include ***

dnl these are all the gst plug-ins, compilable without additional libs
AG_GST_CHECK_PLUGIN(adder)
AG_GST_CHECK_PLUGIN(app)
AG_GST_CHECK_PLUGIN(audioconvert)
AG_GST_CHECK_PLUGIN(audiorate)
AG_GST_CHECK_PLUGIN(audiotestsrc)
AG_GST_CHECK_PLUGIN(ffmpegcolorspace)
AG_GST_CHECK_PLUGIN(gdp)
AG_GST_CHECK_PLUGIN(playback)
AG_GST_CHECK_PLUGIN(audioresample)
AG_GST_CHECK_PLUGIN(subparse)
AG_GST_CHECK_PLUGIN(tcp)
AG_GST_CHECK_PLUGIN(typefind)
AG_GST_CHECK_PLUGIN(videotestsrc)
AG_GST_CHECK_PLUGIN(videorate)
AG_GST_CHECK_PLUGIN(videoscale)
AG_GST_CHECK_PLUGIN(volume)

dnl check for gstreamer core features (subsystems)
dnl FIXME: this assumes srcdir == builddir for uninstalled setups
GST_CONFIGPATH=`$PKG_CONFIG --variable=includedir gstreamer-0.10`"/gst/gstconfig.h"
AG_GST_PARSE_SUBSYSTEM_DISABLES($GST_CONFIGPATH)
AM_CONDITIONAL(USE_XML, test $GST_DISABLE_XML != "1")

dnl disable plug-ins that require libxml2's HTML support if it is not available
if test "x$HAVE_LIBXML_HTML" != "xyes"; then
  AG_GST_DISABLE_PLUGIN(subparse)
fi

dnl disable plug-ins that require sys/socket.h if it is not available
if test "x$HAVE_SYS_SOCKET_H" != "xyes"; then
  AG_GST_DISABLE_PLUGIN(tcp)
fi

dnl iso-codes is optional, used by libgsttag
AC_ARG_ENABLE(iso-codes,
AC_HELP_STRING([--enable-iso-codes],[use iso-codes if installed]),
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

dnl *** sys plug-ins ***

echo
AC_MSG_NOTICE([Checking libraries for plugins in sys/])
echo

dnl *** X11 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_X, true)
AG_GST_CHECK_FEATURE(X, [X libraries and plugins],
                  [ximagesink], [
  AC_PATH_XTRA
  ac_cflags_save="$CFLAGS"
  ac_cppflags_save="$CPPFLAGS"
  CFLAGS="$CFLAGS $X_CFLAGS"
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"

  dnl now try to find the HEADER
  AC_CHECK_HEADER(X11/Xlib.h, HAVE_X="yes", HAVE_X="no")

  if test "x$HAVE_X" = "xno"
  then
    AC_MSG_NOTICE([cannot find X11 development files])
  else
    dnl this is much more than we want
    X_LIBS="$X_LIBS $X_PRE_LIBS $X_EXTRA_LIBS"
    dnl AC_PATH_XTRA only defines the path needed to find the X libs,
    dnl it does not add the libs; therefore we add them here
    X_LIBS="$X_LIBS -lX11"
    AC_SUBST(X_CFLAGS)
    AC_SUBST(X_LIBS)
  fi
  AC_SUBST(HAVE_X)
  CFLAGS="$ac_cflags_save"
  CPPFLAGS="$ac_cppflags_save"
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

dnl v4l/v4l2 checks have been moved down because they require X

dnl *** Video 4 Linux ***
dnl for information about the header/define, see sys/v4l/gstv4lelement.h
dnl renamed to GST_V4L in accordance with V4L2 below
translit(dnm, m, l) AM_CONDITIONAL(USE_GST_V4L, true)
AG_GST_CHECK_FEATURE(GST_V4L, [Video 4 Linux], video4linux, [
  AC_CHECK_DECL(VID_TYPE_MPEG_ENCODER, HAVE_GST_V4L="yes", HAVE_GST_V4L="no", [
#include <sys/types.h>
#define _LINUX_TIME_H
#define __user
#include <linux/videodev.h>
  ])

  dnl we can build v4l without Xv, but then we won't have XOverlay support
  if test "x$HAVE_GST_V4L" = "xyes" -a "x$HAVE_XVIDEO" != "xyes"
  then
    AC_MSG_NOTICE([NO XVIDEO FOUND, VIDEO4LINUX WILL BE BUILT])
    AC_MSG_NOTICE([WITHOUT XOVERLAY SUPPORT])
  fi
])

# Optional gudev for device probing
AC_ARG_WITH([gudev],
  AC_HELP_STRING([--with-gudev],
    [device detection with gudev]),
  [],
  [with_gudev=check])
if test x$HAVE_GST_V4L = xyes; then
  if test x$with_gudev != xno; then
      PKG_CHECK_MODULES(GUDEV, [ gudev-1.0 >= 143 ],
          [ have_gudev=yes
            AC_DEFINE(HAVE_GUDEV, 1,
               [Whether gudev is available for device detection])
            AC_DEFINE([G_UDEV_API_IS_SUBJECT_TO_CHANGE], 1, [I know the API is subject to change.])
          ], [
          have_gudev=no
          ])
  else
    have_gudev=no
  fi
fi
AC_SUBST(GUDEV_CFLAGS)
AC_SUBST(GUDEV_LIBS)

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

dnl *** GNOME VFS ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GNOME_VFS, true)
AG_GST_CHECK_FEATURE(GNOME_VFS, [GNOME VFS], gnomevfs, [
  PKG_CHECK_MODULES(GNOME_VFS, gnome-vfs-2.0, [
      HAVE_GNOME_VFS="yes"
      AC_SUBST(GNOME_VFS_CFLAGS)
      AC_SUBST(GNOME_VFS_LIBS)
      GNOME_VFS_MODULES_DIR="`$PKG_CONFIG --variable=modulesdir gnome-vfs-2.0`"
      AC_DEFINE_UNQUOTED(GNOME_VFS_MODULES_DIR, "$GNOME_VFS_MODULES_DIR",
             [The GnomeVFS modules directory.])
    ],[
      HAVE_GNOME_VFS="no"
    ]
    )
])

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
  fi
])

dnl *** libgio ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GIO, true)
AG_GST_CHECK_FEATURE(GIO, [GIO library], gio, [
  PKG_CHECK_MODULES(GIO, gio-2.0 >= 2.16, [
    HAVE_GIO="yes"
    GIO_MODULE_DIR="`$PKG_CONFIG --variable=giomoduledir gio-2.0`"
    AC_DEFINE_UNQUOTED(GIO_MODULE_DIR, "$GIO_MODULE_DIR",
           [The GIO modules directory.])
    GIO_LIBDIR="`$PKG_CONFIG --variable=libdir gio-2.0`"
    AC_DEFINE_UNQUOTED(GIO_LIBDIR, "$GIO_LIBDIR",
           [The GIO library directory.])
  ], [
    HAVE_GIO="no"
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(GIO_CFLAGS)
  AC_SUBST(GIO_LIBS)
  AC_SUBST(GIO_LDFLAGS)
])

dnl *** libvisual ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBVISUAL, true)
AG_GST_CHECK_FEATURE(LIBVISUAL, [libvisual visualization library], libvisual, [
  AG_GST_PKG_CHECK_MODULES(LIBVISUAL, libvisual-0.4 >= 0.4.0)
  if test $HAVE_LIBVISUAL = no
  then
    AG_GST_PKG_CHECK_MODULES(LIBVISUAL, libvisual = 0.2.0)
    if test x$HAVE_LIBVISUAL = xyes; then
      LIBVIS_PLUGINSDIR="`$PKG_CONFIG --variable=pluginsbasedir libvisual-0.2`"
    fi
  else
    if test x$HAVE_LIBVISUAL = xyes; then
      LIBVIS_PLUGINSDIR="`$PKG_CONFIG --variable=pluginsbasedir libvisual-0.4`"
    fi
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
  if test $HAVE_OGG = no
  then
    XIPH_PATH_OGG(HAVE_OGG="yes", HAVE_OGG="no")
    AS_SCRUB_INCLUDE(OGG_CFLAGS)
  fi
])

dnl *** pango ***
translit(dnm, m, l) AM_CONDITIONAL(USE_PANGO, true)
AG_GST_CHECK_FEATURE(PANGO, [Pango font rendering], pango, [
  AG_GST_PKG_CHECK_MODULES(PANGO, pango >= 1.16.0  pangocairo >= 1.16.0)
])

dnl *** theora ***
translit(dnm, m, l) AM_CONDITIONAL(USE_THEORA, true)
AG_GST_CHECK_FEATURE(THEORA, [Xiph Theora video codec], theora, [
  dnl theora uses pkg-config version incorrectly, for pkg-config:
  dnl 1.0 < 1.0RCX < 1.0alphaX < 1.0betaX < 1.0.0
  dnl theoraenc and theoradec appeared in 1.0beta3

  AG_GST_PKG_CHECK_MODULES(THEORA, theoradec theoraenc)
])

dnl *** vorbis ***
dnl AM_PATH_VORBIS only takes two options
translit(dnm, m, l) AM_CONDITIONAL(USE_VORBIS, true)
AG_GST_CHECK_FEATURE(VORBIS, [Xiph Vorbis audio codec], vorbis, [
  AG_GST_PKG_CHECK_MODULES(VORBIS, vorbis >= 1.0 vorbisenc >= 1.0)
  if test $HAVE_VORBIS = no
  then
    XIPH_PATH_VORBIS(HAVE_VORBIS="yes", HAVE_VORBIS="no")
    AS_SCRUB_INCLUDE(VORBIS_CFLAGS)
  fi
])

if test "x$HAVE_VORBIS" = "xyes"; then
  ac_cflags_save="$CFLAGS"
  AC_COMPILE_IFELSE(
    AC_LANG_PROGRAM([
#include <vorbis/codec.h>
                     ],[
vorbis_dsp_state *v;

vorbis_synthesis_restart (v);
                     ]), HAVE_VSR=yes, HAVE_VSR=no)
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
AM_CONDITIONAL(USE_GIO, false)
AM_CONDITIONAL(USE_GNOME_VFS, false)
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
GST_CXXFLAGS="$GLIB_CFLAGS $GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CXXFLAGS)"
GST_CFLAGS="$GLIB_CFLAGS $GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CFLAGS)"
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_CXXFLAGS)
dnl add GCOV libs because libtool strips -fprofile-arcs -ftest-coverage
GST_LIBS="$GST_LIBS \$(GCOV_LIBS)"
AC_SUBST(GST_LIBS)

dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_ALL_LDFLAGS="-no-undefined"
AC_SUBST(GST_ALL_LDFLAGS)

dnl GST_LIB_LDFLAGS
dnl linker flags shared by all libraries
dnl LDFLAGS modifier defining exported symbols from built libraries
dnl (export _gst_foo but not __gst_foo)
GST_LIB_LDFLAGS="-export-symbols-regex ^_?\(gst_\|Gst\|GST_\).*"
AC_SUBST(GST_LIB_LDFLAGS)

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_desc\$\$' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl *** output files ***

AC_CONFIG_FILES(
Makefile
gst-plugins-base.spec
gst/Makefile
gst/adder/Makefile
gst/app/Makefile
gst/audioconvert/Makefile
gst/audiorate/Makefile
gst/audiotestsrc/Makefile
gst/ffmpegcolorspace/Makefile
gst/gdp/Makefile
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
sys/v4l/Makefile
ext/Makefile
ext/alsa/Makefile
ext/cdparanoia/Makefile
ext/gnomevfs/Makefile
ext/gio/Makefile
ext/libvisual/Makefile
ext/ogg/Makefile
ext/pango/Makefile
ext/theora/Makefile
ext/vorbis/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/audio/Makefile
gst-libs/gst/app/Makefile
gst-libs/gst/cdda/Makefile
gst-libs/gst/fft/Makefile
gst-libs/gst/floatcast/Makefile
gst-libs/gst/interfaces/Makefile
gst-libs/gst/netbuffer/Makefile
gst-libs/gst/riff/Makefile
gst-libs/gst/rtp/Makefile
gst-libs/gst/rtsp/Makefile
gst-libs/gst/sdp/Makefile
gst-libs/gst/tag/Makefile
gst-libs/gst/pbutils/Makefile
gst-libs/gst/video/Makefile
tools/Makefile
pkgconfig/Makefile
pkgconfig/gstreamer-audio.pc
pkgconfig/gstreamer-audio-uninstalled.pc
pkgconfig/gstreamer-app.pc
pkgconfig/gstreamer-app-uninstalled.pc
pkgconfig/gstreamer-cdda.pc
pkgconfig/gstreamer-cdda-uninstalled.pc
pkgconfig/gstreamer-fft.pc
pkgconfig/gstreamer-fft-uninstalled.pc
pkgconfig/gstreamer-floatcast.pc
pkgconfig/gstreamer-floatcast-uninstalled.pc
pkgconfig/gstreamer-interfaces.pc
pkgconfig/gstreamer-interfaces-uninstalled.pc
pkgconfig/gstreamer-netbuffer.pc
pkgconfig/gstreamer-netbuffer-uninstalled.pc
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
tests/examples/dynamic/Makefile
tests/examples/overlay/Makefile
tests/examples/seek/Makefile
tests/examples/volume/Makefile
tests/examples/snapshot/Makefile
tests/examples/gio/Makefile
tests/examples/v4l/Makefile
tests/files/Makefile
tests/icles/Makefile
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
    -e 's/.* GST_MAJORMINOR$/#define GST_MAJORMINOR "'$GST_MAJORMINOR'"/' \
    -e "s,.* GST_PACKAGE_NAME$,#define GST_PACKAGE_NAME \"${GST_PACKAGE_NAME}\"," \
    -e 's/.* GST_PACKAGE_ORIGIN$/#define GST_PACKAGE_ORIGIN "Unknown package origin"/' \
    -e 's/.* HAVE_CPU_I386$/#define HAVE_CPU_I386 1/' \
    -e 's/.* HAVE_FGETPOS$/#define HAVE_FGETPOS 1/' \
    -e 's/.* HAVE_FSETPOS$/#define HAVE_FSETPOS 1/' \
    -e 's/.* HAVE_ISO_CODES$/#undef HAVE_ISO_CODES/' \
    -e 's/.* HAVE_LIBXML2$/#define HAVE_LIBXML2 1/' \
    -e 's/.* HAVE_PROCESS_H$/#define HAVE_PROCESS_H 1/' \
    -e 's/.* HAVE_STDLIB_H$/#define HAVE_STDLIB_H 1/' \
    -e 's/.* HAVE_STRING_H$/#define HAVE_STRING_H 1/' \
    -e 's/.* HAVE_SYS_STAT_H$/#define HAVE_SYS_STAT_H 1/' \
    -e 's/.* HAVE_SYS_TYPES_H$/#define HAVE_SYS_TYPES_H 1/' \
    -e 's/.* HAVE_WIN32$/#define HAVE_WIN32 1/' \
    -e 's/.* HAVE_WINSOCK2_H$/#define HAVE_WINSOCK2_H 1/' \
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
    -e 's/.* PLUGINDIR$/#ifdef _DEBUG\n#  define PLUGINDIR PREFIX "\\\\debug\\\\lib\\\\gstreamer-0.10"\n#else\n#  define PLUGINDIR PREFIX "\\\\lib\\\\gstreamer-0.10"\n#endif/' \
    -e 's/.* USE_BINARY_REGISTRY$/#define USE_BINARY_REGISTRY/' \
    -e 's/.* VERSION$/#define VERSION "'$VERSION'"/' \
    -e "s/.* DEFAULT_AUDIOSINK$/#define DEFAULT_AUDIOSINK \"directaudiosink\"/" \
    -e "s/.* DEFAULT_VIDEOSINK$/#define DEFAULT_VIDEOSINK \"directdrawsink\"/" \
    -e "s/.* DEFAULT_VISUALIZER$/#define DEFAULT_VISUALIZER \"goom\"/" \
    config.h.in >win32/common/config.h-new



AC_OUTPUT

AG_GST_OUTPUT_PLUGINS
ORC_OUTPUT

