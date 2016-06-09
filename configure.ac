AC_PREREQ([2.69])

dnl please read gstreamer/docs/random/autotools before changing this file

dnl initialize autoconf
dnl releases only do -Wall, git and prerelease does -Werror too
dnl use a three digit version number for releases, and four for git/pre
AC_INIT([GStreamer Good Plug-ins],[1.8.2],[http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer],[gst-plugins-good])

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability 1.14 no-dist-gzip dist-xz tar-ustar subdir-objects])

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_GIT="no", GST_GIT="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/law/alaw.c])

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
AS_LIBTOOL(GST, 802, 0, 802)

dnl *** required versions of GStreamer stuff ***
GST_REQ=1.8.0
GSTPB_REQ=1.8.1

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl set up gettext
dnl the version check needs to stay here because autopoint greps for it
AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])
AG_GST_GETTEXT([gst-plugins-good-$GST_API_VERSION])

dnl *** check for arguments to configure ***

AG_GST_ARG_DISABLE_FATAL_WARNINGS
AG_GST_ARG_ENABLE_EXTRA_CHECKS

AG_GST_ARG_DEBUG
AG_GST_ARG_PROFILING
AG_GST_ARG_VALGRIND
AG_GST_ARG_GCOV

AG_GST_ARG_EXAMPLES

AG_GST_ARG_WITH_PKG_CONFIG_PATH
AG_GST_ARG_WITH_PACKAGE_NAME
AG_GST_ARG_WITH_PACKAGE_ORIGIN

AG_GST_ARG_WITH_PLUGINS

AG_GST_ARG_ENABLE_EXTERNAL

AG_GST_ARG_ENABLE_EXPERIMENTAL

dnl *** checks for platform ***

dnl * hardware/architecture *

dnl common/m4/gst-arch.m4
dnl check CPU type
AG_GST_ARCH

dnl Determine endianness
AC_C_BIGENDIAN

dnl *** checks for programs ***

dnl find a compiler
AC_PROG_CC
AC_PROG_CC_STDC

dnl determine c++ compiler
AC_PROG_CXX
dnl determine if c++ is available on this system
AC_CHECK_PROG(HAVE_CXX, $CXX, yes, no)

dnl determine c++ preprocessor
dnl FIXME: do we need this ?
AC_PROG_CXXCPP

AC_PROG_OBJC

dnl check if the compiler supports '-c' and '-o' options
AM_PROG_CC_C_O

dnl find an assembler
AM_PROG_AS

dnl check if the compiler supports do while(0) macros
AG_GST_CHECK_DOWHILE_MACROS

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for documentation tools
GTK_DOC_CHECK([1.12])
AG_GST_PLUGIN_DOCS([1.12])

dnl *** checks for libraries ***

dnl check for libm, for sin()
LT_LIB_M
AC_SUBST(LIBM)

AC_CHECK_FUNCS(clock_gettime, [], [
  AC_CHECK_LIB(rt, clock_gettime, [
    AC_DEFINE(HAVE_CLOCK_GETTIME, 1)
    LIBRT="-lrt"
    AC_SUBST([LIBRT])
  ])
])

dnl *** checks for header files ***

dnl check if we have ANSI C header files
AC_HEADER_STDC

dnl used by ext/wavpack
AX_CREATE_STDINT_H

dnl used in gst/udp
AC_CHECK_HEADERS([sys/time.h])

dnl used in gst/udp
AC_CHECK_HEADERS([sys/socket.h])

dnl *** checks for types/defines ***

dnl Check for FIONREAD ioctl declaration.  This check is needed
dnl for the UDP plugin to build on Solaris
GST_CHECK_FIONREAD

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl check if we have GCC inline-asm
AS_GCC_INLINE_ASSEMBLY([HAVE_GCC_ASM=yes], [HAVE_GCC_ASM=no])
if test x$HAVE_GCC_ASM = xyes ; then
  AC_DEFINE(HAVE_GCC_ASM, 1,
    [Define if compiler supports gcc inline assembly])
fi
AM_CONDITIONAL(HAVE_GCC_ASM, test "x$HAVE_GCC_ASM" = "xyes")

dnl *** checks for library functions ***

LIBS_SAVE=$LIBS
LIBS="$LIBS $LIBM"
AC_CHECK_FUNCS(rint sinh cosh asinh fpclass)
LIBS=$LIBS_SAVE

dnl Check whether isinf() is defined by math.h
AC_CACHE_CHECK([for isinf], ac_cv_have_isinf,
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <math.h>]], [[float f = 0.0; int i=isinf(f)]])],[ac_cv_have_isinf="yes"],[ac_cv_have_isinf="no"]))
if test "$ac_cv_have_isinf" = "yes"
then
    AC_DEFINE(HAVE_ISINF, 1, [Define to 1 if you have the `isinf' function.])
fi

dnl Check for mmap (needed by electricfence plugin)
AC_FUNC_MMAP
AM_CONDITIONAL(GST_HAVE_MMAP, test "x$ac_cv_func_mmap_fixed_mapped" = "xyes")

dnl Check for mmap (needed by electricfence plugin)

AC_CHECK_TYPE([struct ip_mreqn], [
              AC_DEFINE(HAVE_IP_MREQN,, [Define if we have struct ip_mreqn])],,
              [#include <netinet/in.h>])

dnl *** checks for dependency libraries ***


dnl GLib
GLIB_REQ=2.40.0
AG_GST_GLIB_CHECK([$GLIB_REQ])

dnl Orc
ORC_CHECK([0.4.17])

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_BASE($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_CONTROLLER($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_NET($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_CHECK($GST_API_VERSION, [$GST_REQ], no)
AG_GST_CHECK_GST_PLUGINS_BASE($GST_API_VERSION, [$GSTPB_REQ], yes)

GST_TOOLS_DIR=`$PKG_CONFIG --variable=toolsdir gstreamer-$GST_API_VERSION`
if test -z $GST_TOOLS_DIR; then
  AC_MSG_ERROR([no tools dir defined in GStreamer pkg-config file; core upgrade needed.])
fi
AC_SUBST(GST_TOOLS_DIR)

AC_MSG_NOTICE(Using GStreamer Core Plugins in $GST_PLUGINS_DIR)
AC_MSG_NOTICE(Using GStreamer Base Plugins in $GSTPB_PLUGINS_DIR)

AM_CONDITIONAL(HAVE_GST_CHECK, test "x$HAVE_GST_CHECK" = "xyes")

dnl Check for documentation xrefs
GLIB_PREFIX="`$PKG_CONFIG --variable=prefix glib-2.0`"
GST_PREFIX="`$PKG_CONFIG --variable=prefix gstreamer-$GST_API_VERSION`"
GSTPB_PREFIX="`$PKG_CONFIG --variable=prefix gstreamer-plugins-base-$GST_API_VERSION`"
AC_SUBST(GLIB_PREFIX)
AC_SUBST(GST_PREFIX)
AC_SUBST(GSTPB_PREFIX)

dnl GTK is optional and used in examples
HAVE_GTK=no
GTK_REQ=3.0.0
if test "x$BUILD_EXAMPLES" = "xyes"; then
  PKG_CHECK_MODULES(GTK, gtk+-3.0 >= $GTK_REQ, HAVE_GTK=yes, HAVE_GTK=no)
  dnl some examples need gtk+-x11
  PKG_CHECK_MODULES(GTK_X11, gtk+-x11-3.0 >= $GTK_REQ, HAVE_GTK_X11=yes, HAVE_GTK_X11=no)
  AC_SUBST(GTK_LIBS)
  AC_SUBST(GTK_CFLAGS)
fi
AM_CONDITIONAL(HAVE_GTK, test "x$HAVE_GTK" = "xyes")
AM_CONDITIONAL(HAVE_GTK_X11, test "x$HAVE_GTK_X11" = "xyes")

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

dnl *** set variables based on configure arguments ***

dnl set license and copyright notice
GST_LICENSE="LGPL"
AC_DEFINE_UNQUOTED(GST_LICENSE, "$GST_LICENSE", [GStreamer license])
AC_SUBST(GST_LICENSE)

dnl set location of plugin directory
AG_GST_SET_PLUGINDIR

dnl set release date/time
AG_GST_SET_PACKAGE_RELEASE_DATETIME_WITH_NANO([$PACKAGE_VERSION_NANO],
  ["${srcdir}/gst-plugins-good.doap"],
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
dnl -Wundef: too many broken headers
AG_GST_SET_ERROR_CFLAGS($FATAL_WARNINGS, [
      -Wmissing-declarations -Wmissing-prototypes -Wredundant-decls
      -Wwrite-strings -Wold-style-definition -Waggregate-return
      -Winit-self -Wmissing-include-dirs -Waddress -Wno-multichar
      -Wnested-externs $NO_WARNINGS])

dnl define an ERROR_CXXFLAGS Makefile variable
AG_GST_SET_ERROR_CXXFLAGS($FATAL_WARNINGS, [
        -Wmissing-declarations -Wredundant-decls
        -Wwrite-strings
        -Winit-self -Wmissing-include-dirs -Waddress -Wno-multichar
        $NO_WARNINGS])

dnl define an ERROR_OBJCFLAGS Makefile variable
AG_GST_SET_ERROR_OBJCFLAGS($FATAL_WARNINGS, [
      -Wmissing-declarations -Wmissing-prototypes -Wredundant-decls
      -Wwrite-strings -Wold-style-definition
      -Winit-self -Wmissing-include-dirs -Wno-multichar
      -Wnested-externs $NO_WARNINGS])

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT($GST_GIT)

dnl used in examples
AG_GST_DEFAULT_ELEMENTS

dnl *** plug-ins to include ***

dnl these are all the gst plug-ins, compilable without additional libs
dnl videofilter is at the top because others depend on it
AG_GST_CHECK_PLUGIN(alpha)
AG_GST_CHECK_PLUGIN(apetag)
AG_GST_CHECK_PLUGIN(audiofx)
AG_GST_CHECK_PLUGIN(audioparsers)
AG_GST_CHECK_PLUGIN(auparse)
AG_GST_CHECK_PLUGIN(autodetect)
AG_GST_CHECK_PLUGIN(avi)
AG_GST_CHECK_PLUGIN(cutter)
AG_GST_CHECK_PLUGIN(debugutils)
AG_GST_CHECK_PLUGIN(deinterlace)
AG_GST_CHECK_PLUGIN(dtmf)
AG_GST_CHECK_PLUGIN(effectv)
AG_GST_CHECK_PLUGIN(equalizer)
AG_GST_CHECK_PLUGIN(flv)
AG_GST_CHECK_PLUGIN(flx)
AG_GST_CHECK_PLUGIN(goom)
AG_GST_CHECK_PLUGIN(goom2k1)
AG_GST_CHECK_PLUGIN(icydemux)
AG_GST_CHECK_PLUGIN(id3demux)
AG_GST_CHECK_PLUGIN(imagefreeze)
AG_GST_CHECK_PLUGIN(interleave)
AG_GST_CHECK_PLUGIN(isomp4)
AG_GST_CHECK_PLUGIN(law)
AG_GST_CHECK_PLUGIN(level)
AG_GST_CHECK_PLUGIN(matroska)
AG_GST_CHECK_PLUGIN(monoscope)
AG_GST_CHECK_PLUGIN(multifile)
AG_GST_CHECK_PLUGIN(multipart)
AG_GST_CHECK_PLUGIN(replaygain)
AG_GST_CHECK_PLUGIN(rtp)
AG_GST_CHECK_PLUGIN(rtpmanager)
AG_GST_CHECK_PLUGIN(rtsp)
AG_GST_CHECK_PLUGIN(shapewipe)
AG_GST_CHECK_PLUGIN(smpte)
AG_GST_CHECK_PLUGIN(spectrum)
AG_GST_CHECK_PLUGIN(udp)
AG_GST_CHECK_PLUGIN(videobox)
AG_GST_CHECK_PLUGIN(videocrop)
AG_GST_CHECK_PLUGIN(videofilter)
AG_GST_CHECK_PLUGIN(videomixer)
AG_GST_CHECK_PLUGIN(wavenc)
AG_GST_CHECK_PLUGIN(wavparse)
AG_GST_CHECK_PLUGIN(y4m)

dnl disable experimental plug-ins
if test "x$BUILD_EXPERIMENTAL" != "xyes"; then
  AG_GST_DISABLE_PLUGIN(monoscope)
fi

dnl *** sys plug-ins ***

echo
AC_MSG_NOTICE([Checking libraries for plugins in sys/])
echo

dnl DirectSound
translit(dnm, m, l) AM_CONDITIONAL(USE_DIRECTSOUND, true)
AG_GST_CHECK_FEATURE(DIRECTSOUND, [DirectSound plug-in], directsoundsink, [
  HAVE_DIRECTSOUND="no"
  save_CFLAGS="$CFLAGS"
  save_LDFLAGS="$LDFLAGS"
  save_LIBS="$LIBS"
  CFLAGS="$CFLAGS $DIRECTSOUND_CFLAGS"
  LDFLAGS="$LDFLAGS $DIRECTSOUND_LDFLAGS"
  LIBS="$LIBS -ldsound -ldxerr9 -luser32 -lole32"
  AC_MSG_CHECKING(for DirectSound LDFLAGS)
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <windows.h>
#include <dxerr9.h>
#include <dsound.h>
]], [[
  DXGetErrorString9 (0);
  DirectSoundCreate(NULL, NULL, NULL);
  CLSIDFromString(NULL, NULL);
]])
],
    [HAVE_DIRECTSOUND="yes"],
    [HAVE_DIRECTSOUND="no"])
  AC_MSG_RESULT($HAVE_DIRECTSOUND)
  CFLAGS=$save_CFLAGS
  LDFLAGS=$save_LDFLAGS
  LIBS=$save_LIBS

  if test "x$HAVE_DIRECTSOUND" = "xyes";  then
    dnl this is much more than we want
    DIRECTSOUND_LIBS="-ldsound -ldxerr9 -luser32 -lole32"
    AC_SUBST(DIRECTSOUND_CFLAGS)
    AC_SUBST(DIRECTSOUND_LDFLAGS)
    AC_SUBST(DIRECTSOUND_LIBS)
  fi
  AC_SUBST(HAVE_DIRECTSOUND)
])

dnl *** Win32 WaveOut ***
translit(dnm, m, l) AM_CONDITIONAL(USE_WAVEFORM, true)
AG_GST_CHECK_FEATURE(WAVEFORM, [Win32 WaveForm], waveformsink, [
  AC_CHECK_HEADER(mmsystem.h, HAVE_WAVEFORM="yes", HAVE_WAVEFORM="no", [#include <windows.h>])
])

dnl *** OSS audio *** (Linux, *BSD)
translit(dnm, m, l) AM_CONDITIONAL(USE_OSS, true)
AG_GST_CHECK_FEATURE(OSS, [OSS audio], ossaudio, [
 HAVE_OSS="yes"
dnl Linux and newer BSD versions :
 AC_CHECK_HEADER(sys/soundcard.h, [
   AC_DEFINE(HAVE_OSS_INCLUDE_IN_SYS,, [Define if OSS includes are in /sys/])
   ] , [
dnl Some old BSD versions and also newer OpenBSD versions :
   AC_CHECK_HEADER(soundcard.h, [
     AC_DEFINE(HAVE_OSS_INCLUDE_IN_ROOT,, [Define if OSS includes are in /])
     ], [
  dnl Some old BSD versions :
     AC_CHECK_HEADER(machine/soundcard.h, [
       AC_DEFINE(HAVE_OSS_INCLUDE_IN_MACHINE,,
         [Define if OSS includes are in /machine/])
       ], [
       HAVE_OSS="no"
     ])
   ])
 ])
])

dnl *** oss4 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OSS4, true)
AG_GST_CHECK_FEATURE(OSS4, [Open Sound System 4], oss4, [
  AC_MSG_CHECKING([Checking if we can build the OSS4 elements])
  AC_CHECK_HEADERS([fcntl.h sys/ioctl.h sys/stat.h sys/types.h],
      [test -z "$OSS4_MISSING_HEADER" && HAVE_OSS4="yes"],
      [OSS4_MISSING_HEADER="yes";HAVE_OSS4="no"])
])

dnl *** Sun Audio ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SUNAUDIO, true)
AG_GST_CHECK_FEATURE(SUNAUDIO, [Sun Audio], sunaudio, [
  AC_CHECK_HEADER(sys/audioio.h, HAVE_SUNAUDIO="yes", HAVE_SUNAUDIO="no")
])

dnl *** OSX Audio ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OSX_AUDIO, true)
HAVE_IOS="no"
AG_GST_CHECK_FEATURE(OSX_AUDIO, [OSX audio], osxaudio, [
  AC_CHECK_HEADER(CoreAudio/CoreAudio.h,
    [HAVE_OSX_AUDIO="yes"],
    [AC_CHECK_HEADER(CoreAudio/CoreAudioTypes.h,
    [
      HAVE_OSX_AUDIO="yes"
      HAVE_IOS="yes"
    ], [
      HAVE_OSX_AUDIO="no"
      HAVE_IOS="no"
    ])]
  )
])
if test "x$HAVE_IOS" = "xyes"; then
  AC_DEFINE(HAVE_IOS, 1, [building for iOS platofrm])
fi
AM_CONDITIONAL(HAVE_IOS, test "x$HAVE_IOS" = "xyes")

dnl *** OS X video ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OSX_VIDEO, true)
HAVE_OSX_VIDEO="no"
AG_GST_CHECK_FEATURE(OSX_VIDEO, [OSX video], osxvideosink, [
  AC_CHECK_HEADER(OpenGL/gl.h, HAVE_OSX_VIDEO="yes", HAVE_OSX_VIDEO="no")

  dnl in case header OpenGL/gl.h is found on other platforms (or older, unsupported OS X)
  dnl also require Snow Leopard or newer

  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
        #include <AvailabilityMacros.h>
        #if MAC_OS_X_VERSION_MIN_REQUIRED < 1060
        #error Too old OSX version
        #endif
      ]], [[return 0;]])],[HAVE_OSX_VIDEO="yes"],[HAVE_OSX_VIDEO="no"])
])

dnl *** Video 4 Linux 2 ***
dnl renamed to GST_V4L2 because of some conflict with kernel headers
translit(dnm, m, l) AM_CONDITIONAL(USE_GST_V4L2, true)
HAVE_GST_V4L2=no
AG_GST_CHECK_FEATURE(GST_V4L2, [Video 4 Linux 2], video4linux2, [
  AC_MSG_CHECKING([Checking for video4linux2 header ...])
  AC_CHECK_HEADER(linux/videodev2.h, [ HAVE_GST_V4L2=yes ],
  [
    AC_CHECK_HEADER(sys/videodev2.h, [ HAVE_GST_V4L2=yes ],
    [
      AC_CHECK_HEADER(sys/videoio.h, [ HAVE_GST_V4L2=yes ])
    ])
  ])
  if test "x$HAVE_VIDEODEV" = "xno"; then
     AC_MSG_WARN([video4linux2 was not found])
  fi
])

# Optional gudev for device probing
AC_ARG_WITH([gudev],
  AS_HELP_STRING([--with-gudev],[device detection with gudev]),
  [],
  [with_gudev=check])
if test x$HAVE_GST_V4L2 = xyes; then
  if test x$with_gudev != xno; then
    PKG_CHECK_MODULES(GUDEV, [ gudev-1.0 >= 147 ],
      [ AC_DEFINE(HAVE_GUDEV, 1,
          [Whether gudev is available for device detection])
      ],
      [true])
  fi
fi

AC_SUBST(GUDEV_CFLAGS)
AC_SUBST(GUDEV_LIBS)

# Make libv4l2 non-automagic
AC_ARG_WITH([libv4l2],
  AS_HELP_STRING([--with-libv4l2],[support video buffer conversion using libv4l2]),
  [],
  [with_libv4l2=check])
if test x$HAVE_GST_V4L2 = xyes; then
  if test x$with_libv4l2 != xno; then
      PKG_CHECK_MODULES(LIBV4L2, libv4l2,
          [ have_libv4l2=yes
            AC_DEFINE(HAVE_LIBV4L2, 1,
	        [Whether libv4l2 is available for video buffer conversion])
          ], [
          have_libv4l2=no
          ])
  else
    have_libv4l2=no
  fi
fi

dnl Allow enabling v4l2 device probing
AC_ARG_ENABLE(
  v4l2-probe,
  AC_HELP_STRING(
    [--enable-v4l2-probe],
    [enable V4L2 plugin to probe devices @<:@default=no@:>@]))
if test "x$enable_v4l2_probe" = xyes; then
  AC_DEFINE(GST_V4L2_ENABLE_PROBE, 1,
    [Define if Video4Linux probe shall be run at plugin load])
fi

dnl Check for X11
translit(dnm, m, l) AM_CONDITIONAL(USE_X, true)
AG_GST_CHECK_FEATURE(X, [X libraries and plugins],
                  [ximagesrc], [
  PKG_CHECK_MODULES([X], [x11], [
    HAVE_X="yes"
    dnl check for Xfixes
    PKG_CHECK_MODULES([XFIXES], [ xfixes ], [
      AC_DEFINE(HAVE_XFIXES, 1, [Defined if Xfixes is available])
    ], [ HAVE_XFIXES="no" ])

    dnl check for Xdamage
    PKG_CHECK_MODULES([XDAMAGE], [ xdamage ], [
      AC_DEFINE(HAVE_XDAMAGE, 1, [Defined if Xdamage is available])
    ], [ HAVE_XDAMAGE="no" ])

    dnl check for X Shm
    PKG_CHECK_MODULES([XEXT], [ xext ], [
      AC_CHECK_LIB([Xext], [ XShmAttach ], [
        AC_DEFINE(HAVE_XSHM, 1, [Defined if XShm is available])
        XSHM_LIBS="$XEXT_LIBS"
        XSHM_CFLAGS="$XEXT_CFLAGS"
      ], [ HAVE_XEXT="no" ] , [ $X_LIBS ])
    ])
    AC_SUBST(XSHM_LIBS)
    AC_SUBST(XSHM_CFLAGS)
  ], [ HAVE_X="no" ])
])

dnl *** ext plug-ins ***
dnl keep this list sorted alphabetically !

if test "x$BUILD_EXTERNAL" = "xyes"; then

echo
AC_MSG_NOTICE([Checking libraries for plugins in ext/])
echo

dnl *** aalib ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AALIB, true)
AG_GST_CHECK_FEATURE(AALIB, [aalib ASCII Art library], aasink, [
  AM_PATH_AALIB(, HAVE_AALIB=yes, HAVE_AALIB=no)
])

dnl *** cairo ***
translit(dnm, m, l) AM_CONDITIONAL(USE_CAIRO, true)
AG_GST_CHECK_FEATURE(CAIRO, [Cairo graphics rendering and gobject bindings], cairo, [
  AG_GST_PKG_CHECK_MODULES(CAIRO, cairo >= 1.10.0 cairo-gobject >= 1.10.0)
])

dnl *** FLAC ***
translit(dnm, m, l) AM_CONDITIONAL(USE_FLAC, true)
AG_GST_CHECK_FEATURE(FLAC, [FLAC lossless audio], flac, [
  AG_GST_PKG_CHECK_MODULES(FLAC, flac >= 1.1.4)
])

dnl *** GDK pixbuf ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GDK_PIXBUF, true)
AG_GST_CHECK_FEATURE(GDK_PIXBUF, [GDK pixbuf], gdkpixbuf, [
  AG_GST_PKG_CHECK_MODULES(GDK_PIXBUF, gdk-pixbuf-2.0 >= 2.8.0)
])

dnl *** Jack ***
translit(dnm, m, l) AM_CONDITIONAL(USE_JACK, true)
AG_GST_CHECK_FEATURE(JACK, Jack, jack, [
  PKG_CHECK_MODULES(JACK, jack >= 0.99.10, HAVE_JACK="yes", HAVE_JACK="no")
  AC_SUBST(JACK_CFLAGS)
  AC_SUBST(JACK_LIBS)

  AG_GST_PKG_CHECK_MODULES(JACK_0_120_1, jack >= 0.120.1 jack < 1.0)
  if test x$HAVE_JACK_0_120_1 = xyes; then
    AC_DEFINE(HAVE_JACK_0_120_1, 1, [defined if jack >= 0.120.1 is available])
  fi
  AG_GST_PKG_CHECK_MODULES(JACK_1_9_7, jack >= 1.9.7)
  if test x$HAVE_JACK_1_9_7 = xyes; then
    AC_DEFINE(HAVE_JACK_1_9_7, 1, [defined if jack >= 1.9.7 is available])
  fi

])

dnl *** jpeg ***
dnl FIXME: we could use header checks here as well IMO
translit(dnm, m, l) AM_CONDITIONAL(USE_JPEG, true)
AG_GST_CHECK_FEATURE(JPEG, [jpeg library], jpeg, [
  AC_ARG_WITH(jpeg-mmx,
    [  --with-jpeg-mmx, path to MMX'ified JPEG library])
  OLD_LIBS="$LIBS"
  if test x$with_jpeg_mmx != x; then
    LIBS="$LIBS -L$with_jpeg_mmx"
  fi
  AC_CHECK_LIB(jpeg-mmx, jpeg_set_defaults, HAVE_JPEG="yes", HAVE_JPEG="no")
  JPEG_LIBS="$LIBS -ljpeg-mmx"
  LIBS="$OLD_LIBS"
  if test x$HAVE_JPEG != xyes; then
    AC_CHECK_LIB(jpeg, jpeg_set_defaults, HAVE_JPEG="yes", HAVE_JPEG="no")
    JPEG_LIBS="-ljpeg"
  fi
  AC_SUBST(JPEG_LIBS)
])

dnl *** libcaca ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBCACA, true)
AG_GST_CHECK_FEATURE(LIBCACA, [libcaca coloured ASCII art], cacasink, [
  AG_GST_PKG_CHECK_MODULES(LIBCACA, caca)
])

dnl *** libdv ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBDV, true)
AG_GST_CHECK_FEATURE(LIBDV, [libdv DV demuxer/decoder], dv, [
  AG_GST_PKG_CHECK_MODULES(LIBDV, libdv >= 0.100)
])

dnl *** libpng ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBPNG, true)
AG_GST_CHECK_FEATURE(LIBPNG, [Portable Network Graphics library], png, [
  AG_GST_PKG_CHECK_MODULES(LIBPNG, libpng >= 1.2)
])

dnl *** pulseaudio ***
translit(dnm, m, l) AM_CONDITIONAL(USE_PULSE, true)
AG_GST_CHECK_FEATURE(PULSE, [pulseaudio plug-in], pulseaudio, [

  dnl used in ext/pulse/pulseutil.c
  AC_CHECK_HEADERS([process.h])

  AG_GST_PKG_CHECK_MODULES(PULSE, libpulse >= 2.0)
])

dnl *** dv1394 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DV1394, true)
AG_GST_CHECK_FEATURE(DV1394, [raw1394 and avc1394 library], 1394, [
  dnl first test for libraw1394 >= 2.0.0
  AG_GST_PKG_CHECK_MODULES(RAW1394, libraw1394 >= 2.0.0)
  AG_GST_PKG_CHECK_MODULES(LIBIEC61883, libiec61883 >= 1.0.0)
  dnl now see how far we got
  if test x$HAVE_RAW1394 = xyes && \
     test x$HAVE_LIBIEC61883 = xyes; then
       AG_GST_CHECK_LIBHEADER(AVC1394,
        avc1394, avc1394_send_command, $RAW1394_LIBS,
	libavc1394/avc1394.h, AVC1394_LIBS="-lavc1394")
       AG_GST_CHECK_LIBHEADER(ROM1394,
        rom1394, rom1394_free_directory, $RAW1394_LIBS,
        libavc1394/rom1394.h, ROM1394_LIBS="-lrom1394")
       if test x$HAVE_AVC1394 = xyes && \
          test x$HAVE_AVC1394 = xyes && \
	  test x$HAVE_ROM1394 = xyes; then
	  HAVE_DV1394=yes
          DV1394_LIBS="$RAW1394_LIBS $AVC1394_LIBS $ROM1394_LIBS $LIBIEC61883_LIBS"
          DV1394_CFLAGS="$RAW1394_CFLAGS $LIBIEC61883_CFLAGS -DHAVE_LIBIEC61883"
          AC_SUBST(DV1394_CFLAGS)
          AC_SUBST(DV1394_LIBS)
	fi
  else
       if test x$HAVE_RAW1394 = xno; then
	  AG_GST_PKG_CHECK_MODULES(RAW1394, libraw1394 >= 1.1.0)
	  AG_GST_CHECK_LIBHEADER(AVC1394,
	   avc1394, avc1394_send_command, $RAW1394_LIBS,
	   libavc1394/avc1394.h, AVC1394_LIBS="-lavc1394")
	  AG_GST_CHECK_LIBHEADER(ROM1394,
	   rom1394, rom1394_free_directory, $RAW1394_LIBS,
	   libavc1394/rom1394.h, ROM1394_LIBS="-lrom1394")
          if test x$HAVE_RAW1394 = xyes && \
	     test x$HAVE_AVC1394 = xyes && \
	     test x$HAVE_ROM1394 = xyes; then
	     HAVE_DV1394=yes
	     DV1394_LIBS="$RAW1394_LIBS $AVC1394_LIBS $ROM1394_LIBS"
	     DV1394_CFLAGS="$RAW1394_CFLAGS"
	     if test x$HAVE_LIBIEC61883 = xyes; then
	       DV1394_CFLAGS="$RAW1394_CFLAGS $LIBIEC61883_CFLAGS -DHAVE_LIBIEC61883"
	       DV1394_LIBS="$DV1394_LIBS $LIBIEC61883_LIBS"
	     fi
             AC_SUBST(DV1394_CFLAGS)
             AC_SUBST(DV1394_LIBS)
           fi
	 fi
  fi
])
AM_CONDITIONAL(USE_LIBIEC61883, [ test "x${HAVE_LIBIEC61883}" = xyes ] )

dnl *** shout2 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SHOUT2, true)
AG_GST_CHECK_FEATURE(SHOUT2, [Shoutcast/Icecast client library], shout2, [
  AG_GST_PKG_CHECK_MODULES(SHOUT2, shout >= 2.0)
])

dnl *** soup ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SOUP, true)
AG_GST_CHECK_FEATURE(SOUP, [soup http client plugin (2.4)], souphttpsrc, [
  PKG_CHECK_MODULES(SOUP, libsoup-2.4 >= 2.48, [HAVE_SOUP="yes"], [HAVE_SOUP="no"])
  AC_SUBST(SOUP_CFLAGS)
  AC_SUBST(SOUP_LIBS)
])

dnl *** speex ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SPEEX, true)
AG_GST_CHECK_FEATURE(SPEEX, [speex speech codec], speex, [
  AG_GST_PKG_CHECK_MODULES(SPEEX, speex >= 1.1.6)
])

dnl *** taglib ***
translit(dnm, m, l) AM_CONDITIONAL(USE_TAGLIB, true)
AG_GST_CHECK_FEATURE(TAGLIB, [taglib tagging library], taglib, [
  AG_GST_PKG_CHECK_MODULES(TAGLIB, taglib >= 1.5)
  if test "x$HAVE_CXX" != "xyes"; then
    USE_TAGLIB=false
    AC_MSG_NOTICE([Not building taglib plugin: no C++ compiler found])
  else
    dnl work around bug in taglib 1.5 headers, remove once there is a 1.6
    AS_CXX_COMPILER_FLAG([-Wno-attributes], [
        TAGLIB_CXXFLAGS="$TAGLIB_CFLAGS -Wno-attributes"
      ], [
        TAGLIB_CXXFLAGS="$TAGLIB_CFLAGS"
    ])
    AC_SUBST(TAGLIB_CXXFLAGS)
  fi
])

dnl *** vpx ***
translit(dnm, m, l) AM_CONDITIONAL(USE_VPX, vpx)
AG_GST_CHECK_FEATURE(VPX, [VPX decoder], vpx, [
  PKG_CHECK_MODULES(VPX, vpx >= 1.1.0, [HAVE_VPX="yes"], [HAVE_VPX="no"])
  HAVE_VP8=no
  HAVE_VP9=no

  if test $HAVE_VPX = "yes"; then
    OLD_LIBS="$LIBS"
    OLD_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $VPX_CFLAGS"
    LIBS="$LIBS $VPX_LIBS"
    AC_CHECK_LIB(vpx, vpx_codec_vp8_cx_algo, [
      HAVE_VP8=yes
      AC_DEFINE(HAVE_VP8_ENCODER, 1, [Defined if the VP8 encoder is available])
    ])
    AC_CHECK_LIB(vpx, vpx_codec_vp8_dx_algo, [
      HAVE_VP8=yes
      AC_DEFINE(HAVE_VP8_DECODER, 1, [Defined if the VP8 decoder is available])
    ])

    PKG_CHECK_MODULES(VPX_130, vpx >= 1.3.0, [
      AC_CHECK_LIB(vpx, vpx_codec_vp9_cx_algo, [
        HAVE_VP9=yes
        AC_DEFINE(HAVE_VP9_ENCODER, 1, [Defined if the VP9 encoder is available])
      ])
      AC_CHECK_LIB(vpx, vpx_codec_vp9_dx_algo, [
        HAVE_VP9=yes
        AC_DEFINE(HAVE_VP9_DECODER, 1, [Defined if the VP9 decoder is available])
      ])
    ], [true])

    PKG_CHECK_MODULES(VPX_140, vpx >= 1.4.0, [
      AC_DEFINE(HAVE_VPX_1_4, 1, [Defined if the VPX library version is 1.4 or bigger])
    ], [true])

    LIBS="$OLD_LIBS"
    CFLAGS="$OLD_CFLAGS"
  fi
  AC_SUBST(VPX_LIBS)
  AC_SUBST(VPX_CFLAGS)
])

dnl *** wavpack ***
translit(dnm, m, l) AM_CONDITIONAL(USE_WAVPACK, true)
AG_GST_CHECK_FEATURE(WAVPACK, [wavpack plug-in], wavpack, [
  AG_GST_PKG_CHECK_MODULES(WAVPACK, wavpack >= 4.60.0)
])

dnl *** qtdemux & matroska prefer to have zlib ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ZLIB, true)
AG_GST_CHECK_FEATURE(ZLIB, [zlib support for qtdemux/matroska],, [
  AG_GST_CHECK_LIBHEADER(ZLIB,
    z, uncompress,, zlib.h, [
    HAVE_ZLIB="yes"
    ZLIB_LIBS="-lz"
    AC_SUBST(ZLIB_LIBS)
  ])
])

dnl *** matroska prefers to have bz2 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_BZ2, true)
AG_GST_CHECK_FEATURE(BZ2, [bz2 library for matroska ],, [
  save_LIBS=$LIBS
  LIBS="$LIBS -lbz2"
  AC_MSG_CHECKING([for BZ2_bzlibVersion in -lbz2])
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <bzlib.h>]], [[const char *version = BZ2_bzlibVersion ();]])],[HAVE_BZ2=yes],[HAVE_BZ2=no])
  AC_MSG_RESULT($HAVE_BZ2)
  LIBS=$save_LIBS
  if test "x$HAVE_BZ2" = "xyes"; then
    BZ2_LIBS="-lbz2"
  fi
  AC_SUBST(BZ2_LIBS)
])

else

dnl not building plugins with external dependencies,
dnl but we still need to set the conditionals
AM_CONDITIONAL(USE_AALIB, false)
AM_CONDITIONAL(USE_BZ2, false)
AM_CONDITIONAL(USE_CAIRO, false)
AM_CONDITIONAL(USE_DIRECTSOUND, false)
AM_CONDITIONAL(USE_DV1394, false)
AM_CONDITIONAL(USE_FLAC, false)
AM_CONDITIONAL(USE_GDK_PIXBUF, false)
AM_CONDITIONAL(USE_JACK, false)
AM_CONDITIONAL(USE_JPEG, false)
AM_CONDITIONAL(USE_LIBCACA, false)
AM_CONDITIONAL(USE_LIBDV, false)
AM_CONDITIONAL(USE_LIBIEC61883, false)
AM_CONDITIONAL(USE_LIBPNG, false)
AM_CONDITIONAL(USE_OSS, false)
AM_CONDITIONAL(USE_OSS4, false)
AM_CONDITIONAL(USE_OSX_AUDIO, false)
AM_CONDITIONAL(USE_OSX_VIDEO, false)
AM_CONDITIONAL(USE_PULSE, false)
AM_CONDITIONAL(USE_SHOUT2, false)
AM_CONDITIONAL(USE_SOUP, false)
AM_CONDITIONAL(USE_SPEEX, false)
AM_CONDITIONAL(USE_SUNAUDIO, false)
AM_CONDITIONAL(USE_TAGLIB, false)
AM_CONDITIONAL(USE_VPX, false)
AM_CONDITIONAL(USE_WAVEFORM, false)
AM_CONDITIONAL(USE_WAVPACK, false)
AM_CONDITIONAL(USE_X, false)
AM_CONDITIONAL(USE_ZLIB, false)

fi dnl of EXT plugins

dnl *** finalize CFLAGS, LDFLAGS, LIBS

dnl Overview:
dnl GST_OPTION_CFLAGS:  common flags for profiling, debugging, errors, ...
dnl GST_*:              flags shared by all built objects
dnl GST_ALL_LDFLAGS:    linker flags shared by all
dnl GST_LIB_LDFLAGS:    not needed, we don't install libraries
dnl GST_LT_LDFLAGS:     library versioning of our libraries
dnl GST_PLUGIN_LDFLAGS: flags to be used for all plugins

dnl GST_OPTION_CFLAGS
if test "x$USE_DEBUG" = xyes; then
   PROFILE_CFLAGS="-g"
fi
AC_SUBST(PROFILE_CFLAGS)

if test "x$PACKAGE_VERSION_NANO" = "x1"; then
  dnl Define _only_ for git (not pre-releases or releases)
  DEPRECATED_CFLAGS="-DGST_DISABLE_DEPRECATED"
else
  DEPRECATED_CFLAGS=""
fi
AC_SUBST(DEPRECATED_CFLAGS)

dnl every flag in GST_OPTION_CFLAGS, GST_OPTION_CXXFLAGS and GST_OPTION_OBJCFLAGS can be overridden
dnl at make time with e.g. make ERROR_CFLAGS=""
GST_OPTION_CFLAGS="\$(WARNING_CFLAGS) \$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
GST_OPTION_CXXFLAGS="\$(WARNING_CXXFLAGS) \$(ERROR_CXXFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
GST_OPTION_OBJCFLAGS="\$(WARNING_OBJCFLAGS) \$(ERROR_OBJCFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
AC_SUBST(GST_OPTION_CFLAGS)
AC_SUBST(GST_OPTION_CXXFLAGS)
AC_SUBST(GST_OPTION_OBJCFLAGS)

dnl our libraries need to be versioned correctly
AC_SUBST(GST_LT_LDFLAGS)

dnl FIXME: do we want to rename to GST_ALL_* ?
dnl prefer internal headers to already installed ones
dnl also add builddir include for enumtypes and marshal
dnl add ERROR_CFLAGS, but overridable
GST_CFLAGS="$GST_CFLAGS -DGST_USE_UNSTABLE_API"
GST_CXXFLAGS="-I\$(top_srcdir)/gst-libs $GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CXXFLAGS)"
GST_OBJCFLAGS="-I\$(top_srcdir)/gst-libs $GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_OBJCFLAGS)"
GST_CFLAGS="-I\$(top_srcdir)/gst-libs $GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CFLAGS)"
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_CXXFLAGS)
AC_SUBST(GST_OBJCFLAGS)
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

$MKDIR_P tests/check/orc

dnl keep this alphabetic per directory, please
AC_CONFIG_FILES(
Makefile
gst/Makefile
gst/alpha/Makefile
gst/apetag/Makefile
gst/audiofx/Makefile
gst/audioparsers/Makefile
gst/auparse/Makefile
gst/autodetect/Makefile
gst/avi/Makefile
gst/cutter/Makefile
gst/deinterlace/Makefile
gst/dtmf/Makefile
gst/debugutils/Makefile
gst/effectv/Makefile
gst/equalizer/Makefile
gst/flv/Makefile
gst/goom/Makefile
gst/goom2k1/Makefile
gst/id3demux/Makefile
gst/icydemux/Makefile
gst/imagefreeze/Makefile
gst/interleave/Makefile
gst/isomp4/Makefile
gst/law/Makefile
gst/level/Makefile
gst/matroska/Makefile
gst/monoscope/Makefile
gst/multifile/Makefile
gst/multipart/Makefile
gst/replaygain/Makefile
gst/rtp/Makefile
gst/rtpmanager/Makefile
gst/rtsp/Makefile
gst/shapewipe/Makefile
gst/smpte/Makefile
gst/spectrum/Makefile
gst/udp/Makefile
gst/videobox/Makefile
gst/videocrop/Makefile
gst/videofilter/Makefile
gst/videomixer/Makefile
gst/wavenc/Makefile
gst/wavparse/Makefile
gst/flx/Makefile
gst/y4m/Makefile
ext/Makefile
ext/aalib/Makefile
ext/cairo/Makefile
ext/dv/Makefile
ext/flac/Makefile
ext/gdk_pixbuf/Makefile
ext/jack/Makefile
ext/jpeg/Makefile
ext/libcaca/Makefile
ext/libpng/Makefile
ext/pulse/Makefile
ext/raw1394/Makefile
ext/shout2/Makefile
ext/soup/Makefile
ext/speex/Makefile
ext/taglib/Makefile
ext/vpx/Makefile
ext/wavpack/Makefile
sys/Makefile
sys/directsound/Makefile
sys/oss/Makefile
sys/oss4/Makefile
sys/osxaudio/Makefile
sys/osxvideo/Makefile
sys/sunaudio/Makefile
sys/v4l2/Makefile
sys/waveform/Makefile
sys/ximage/Makefile
po/Makefile.in
tests/Makefile
tests/check/Makefile
tests/examples/Makefile
tests/examples/audiofx/Makefile
tests/examples/cairo/Makefile
tests/examples/equalizer/Makefile
tests/examples/jack/Makefile
tests/examples/level/Makefile
tests/examples/pulse/Makefile
tests/examples/rtp/Makefile
tests/examples/shapewipe/Makefile
tests/examples/spectrum/Makefile
tests/examples/v4l2/Makefile
tests/files/Makefile
tests/icles/Makefile
common/Makefile
common/m4/Makefile
m4/Makefile
docs/Makefile
docs/plugins/Makefile
docs/version.entities
pkgconfig/Makefile
pkgconfig/gstreamer-plugins-good-uninstalled.pc
gst-plugins-good.spec
)

dnl Create the config.h file for Visual Studio builds
dnl Beware of spaces and /'s in some of the shell variable contents.
sed \
    -e 's/.*config.h.in.*autoheader.*/\/* Autogenerated config.h created for win32 Visual Studio builds *\/\n\n\/* PREFIX -- specifically added for Windows for easier moving *\/\n#define PREFIX "C:\\\\gstreamer"\n\n#define GST_INSTALL_PLUGINS_HELPER PREFIX "\\\\libexec\\\\gst-install-plugins-helper.exe"/' \
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
    -e 's/.* HAVE_LIBXML2$/#define HAVE_LIBXML2 1/' \
    -e 's/.* HAVE_PROCESS_H$/#define HAVE_PROCESS_H 1/' \
    -e 's/.* HAVE_STDLIB_H$/#define HAVE_STDLIB_H 1/' \
    -e 's/.* HAVE_STRING_H$/#define HAVE_STRING_H 1/' \
    -e 's/.* HAVE_SYS_STAT_H$/#define HAVE_SYS_STAT_H 1/' \
    -e 's/.* HAVE_SYS_TYPES_H$/#define HAVE_SYS_TYPES_H 1/' \
    -e 's/.* HAVE_WIN32$/#define HAVE_WIN32 1/' \
    -e 's/.* HOST_CPU$/#define HOST_CPU "i686"/' \
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
    -e "s/.* DEFAULT_AUDIOSRC$/#define DEFAULT_AUDIOSRC \"audiotestsrc\"/" \
    -e "s/.* DEFAULT_VIDEOSRC$/#define DEFAULT_VIDEOSRC \"videotestsrc\"/" \
    -e "s/.* DEFAULT_VISUALIZER$/#define DEFAULT_VISUALIZER \"goom\"/" \
    config.h.in >win32/common/config.h-new

AC_OUTPUT

AG_GST_OUTPUT_PLUGINS
ORC_OUTPUT

