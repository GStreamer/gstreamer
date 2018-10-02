AC_PREREQ([2.69])

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, git and prerelease does Werror too
AC_INIT([GStreamer Bad Plug-ins],[1.14.4],[http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer],[gst-plugins-bad])

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability 1.14 no-dist-gzip dist-xz tar-ustar subdir-objects])

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_GIT="no", GST_GIT="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([docs/plugins/gst-plugins-bad-plugins-docs.sgml])

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

dnl PKG_CONFIG_SYSROOT_DIR is a valid environment variable
m4_pattern_allow(PKG_CONFIG_SYSROOT_DIR)

dnl our libraries and install dirs use GST_API_VERSION in the filename
dnl to allow side-by-side installation of different API versions
GST_API_VERSION=1.0
AC_SUBST(GST_API_VERSION)
AC_DEFINE_UNQUOTED(GST_API_VERSION, "$GST_API_VERSION",
  [GStreamer API Version])

AG_GST_LIBTOOL_PREPARE

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
dnl sets GST_LT_LDFLAGS
AS_LIBTOOL(GST, 1404, 0, 1404)

dnl *** required versions of GStreamer stuff ***
GST_REQ=1.14.4
GSTPB_REQ=1.14.4

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl set up gettext
dnl the version check needs to stay here because autopoint greps for it
AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])
AG_GST_GETTEXT([gst-plugins-bad-$GST_API_VERSION])

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

AG_GST_PKG_CONFIG_PATH

dnl *** checks for platform ***
AG_GST_PLATFORM

dnl * hardware/architecture *

dnl common/m4/gst-arch.m4
dnl check CPU type
AG_GST_ARCH

dnl Determine endianness
AC_C_BIGENDIAN

dnl *** software ***

dnl check for large file support
dnl affected plugins must include config.h
AC_SYS_LARGEFILE

dnl *** checks for programs ***

dnl find a compiler
AC_PROG_CC
AC_PROG_CC_STDC

dnl determine c++ compiler
AC_PROG_CXX
dnl determine if c++ is available on this system
AC_CHECK_PROG(HAVE_CXX, $CXX, yes, no)

AC_PROG_OBJC

dnl check if the compiler supports '-c' and '-o' options
AM_PROG_CC_C_O

dnl check if the compiler supports do while(0) macros
AG_GST_CHECK_DOWHILE_MACROS

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for gobject-introspection
GOBJECT_INTROSPECTION_CHECK([1.31.1])

dnl check for documentation tools
GTK_DOC_CHECK([1.12])
AG_GST_PLUGIN_DOCS([1.12])

dnl *** checks for libraries ***

dnl check for libm, for sin() etc.
LT_LIB_M
AC_SUBST(LIBM)

AC_FUNC_MMAP

dnl check for pthreads
AX_PTHREAD

dnl *** checks for header files ***

AC_CHECK_HEADERS([unistd.h], HAVE_UNISTD_H=yes)
AM_CONDITIONAL(HAVE_UNISTD_H, test "x$HAVE_UNISTD_H" = "xyes")

if test "x$HAVE_UNISTD_H" != "xyes"; then
  GST_PLUGINS_SELECTED=`echo $GST_PLUGINS_SELECTED | $SED -e s/festival//`
fi

AC_CHECK_HEADERS([sys/time.h])

dnl used by ext/dts
AX_CREATE_STDINT_H

AC_CHECK_HEADERS([pthread.h], HAVE_PTHREAD_H=yes)
AM_CONDITIONAL(HAVE_PTHREAD_H, test "x$HAVE_PTHREAD_H" = "xyes")

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***
AC_CHECK_FUNCS([gmtime_r pipe2])

dnl *** checks for headers ***
AC_CHECK_HEADERS([sys/utsname.h])

dnl *** checks for dependency libraries ***

dnl *** checks for socket and nsl libraries ***
AC_CHECK_FUNC(socket,,[AC_CHECK_LIB(socket,socket)])
AC_CHECK_FUNC(gethostbyname,,[AC_CHECK_LIB(nsl,gethostbyname)])

dnl GLib is required
GLIB_REQ=2.40.0
AG_GST_GLIB_CHECK([$GLIB_REQ])

dnl Needed by plugins that use g_module_*() API
PKG_CHECK_MODULES(GMODULE_NO_EXPORT, gmodule-no-export-2.0)

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_BASE($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_CHECK($GST_API_VERSION, [$GST_REQ], no)
AG_GST_CHECK_GST_CONTROLLER($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_NET($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_PKG_CHECK_MODULES(GST_VIDEO, gstreamer-video-[$GST_API_VERSION], [$GST_REQ], yes)
AG_GST_PKG_CHECK_MODULES(GST_AUDIO, gstreamer-audio-[$GST_API_VERSION], [$GST_REQ], yes)
AG_GST_PKG_CHECK_MODULES(GST_PBUTILS, gstreamer-pbutils-[$GST_API_VERSION], [$GST_REQ], yes)
AG_GST_PKG_CHECK_MODULES(GST_GL, gstreamer-gl-[$GST_API_VERSION], [$GST_REQ], no)
AG_GST_PKG_CHECK_MODULES(GST_SDP, gstreamer-sdp-[$GST_API_VERSION], [$GST_REQ], yes)

if test "x$HAVE_GST_GL" = "xyes"; then

  AC_MSG_CHECKING([GStreamer OpenGL window systems ...])
  GST_GL_WINSYS=`$PKG_CONFIG --variable=gl_winsys gstreamer-gl-1.0`
  AC_MSG_RESULT([$GST_GL_WINSYS])
  GST_GL_HAVE_WINDOW_X11=""
  GST_GL_HAVE_WINDOW_WAYLAND=""
  GST_GL_HAVE_WINDOW_ANDROID=""
  GST_GL_HAVE_WINDOW_COCOA=""
  GST_GL_HAVE_WINDOW_EAGL=""
  case "$GST_GL_WINSYS" in  *x11*) GST_GL_HAVE_WINDOW_X11="1" ;; esac
  case "$GST_GL_WINSYS" in  *wayland*) GST_GL_HAVE_WINDOW_WAYLAND="1" ;; esac
  case "$GST_GL_WINSYS" in  *android*) GST_GL_HAVE_WINDOW_ANDROID="1" ;; esac
  case "$GST_GL_WINSYS" in  *cocoa*) GST_GL_HAVE_WINDOW_COCOA="1" ;; esac
  case "$GST_GL_WINSYS" in  *eagl*) GST_GL_HAVE_WINDOW_EAGL="1" ;; esac

  AC_MSG_CHECKING([GStreamer OpenGL platforms ...])
  GST_GL_PLATFORMS=`$PKG_CONFIG --variable=gl_platforms gstreamer-gl-1.0`
  AC_MSG_RESULT([$GST_GL_PLATFORMS])
  GST_GL_HAVE_PLATFORM_GLX=""
  GST_GL_HAVE_PLATFORM_EGL=""
  GST_GL_HAVE_PLATFORM_CGL=""
  GST_GL_HAVE_PLATFORM_EAGL=""
  case "$GST_GL_PLATFORMS" in  *glx*) GST_GL_HAVE_PLATFORM_GLX="1" ;; esac
  case "$GST_GL_PLATFORMS" in  *egl*) GST_GL_HAVE_PLATFORM_EGL="1" ;; esac
  case "$GST_GL_PLATFORMS" in  *cgl*) GST_GL_HAVE_PLATFORM_CGL="1" ;; esac
  case "$GST_GL_PLATFORMS" in  *eagl*) GST_GL_HAVE_PLATFORM_EAGL="1" ;; esac

  AC_MSG_CHECKING([GStreamer OpenGL apis ...])
  GST_GL_APIS=`$PKG_CONFIG --variable=gl_apis gstreamer-gl-1.0`
  AC_MSG_RESULT([$GST_GL_APIS])
  GST_GL_HAVE_API_GLES2=""
  GST_GL_HAVE_API_GL=""
  case "$GST_GL_APIS" in  *gles2*) GST_GL_HAVE_API_GLES2="1" ;; esac
  case "$GST_GL_APIS" in "gl"|"gl "*|*" gl"|*" gl "*) GST_GL_HAVE_API_GL="1" ;; esac
fi

AM_CONDITIONAL(HAVE_GST_CHECK, test "x$HAVE_GST_CHECK" = "xyes")

AG_GST_CHECK_GST_PLUGINS_BASE($GST_API_VERSION, [$GSTPB_REQ], yes)

dnl check for uninstalled plugin directories for unit tests
AG_GST_CHECK_UNINSTALLED_SETUP([
  AG_GST_CHECK_GST_PLUGINS_GOOD($GST_API_VERSION, [0.11.0])
  AG_GST_CHECK_GST_PLUGINS_UGLY($GST_API_VERSION, [0.11.0])
  AG_GST_CHECK_GST_PLUGINS_LIBAV($GST_API_VERSION, [0.11.0])
])

dnl Check for documentation xrefs
GLIB_PREFIX="`$PKG_CONFIG --variable=prefix glib-2.0`"
GST_PREFIX="`$PKG_CONFIG --variable=prefix gstreamer-$GST_API_VERSION`"
GSTPB_PREFIX="`$PKG_CONFIG --variable=prefix gstreamer-plugins-base-$GST_API_VERSION`"
AC_SUBST(GLIB_PREFIX)
AC_SUBST(GST_PREFIX)
AC_SUBST(GSTPB_PREFIX)

dnl GTK is optional and used in examples
HAVE_GTK=NO
GTK2_REQ=2.14.0
GTK3_REQ=2.91.3
if test "x$BUILD_EXAMPLES" = "xyes"; then
  AC_MSG_CHECKING([which gtk+ version to compile examples against (optional)])
  AC_ARG_WITH([gtk],
    AS_HELP_STRING([--with-gtk=3.0|2.0],[which gtk+ version to compile against (default: 2.0)]),
    [case "$with_gtk" in
        2.0) GTK_REQ=$GTK2_REQ ;;
        3.0) GTK_REQ=$GTK3_REQ ;;
        *) AC_MSG_ERROR([invalid gtk+ version specified]);;
    esac],
    [with_gtk=2.0
     GTK_REQ=$GTK2_REQ])
  AC_MSG_RESULT([$with_gtk (>= $GTK_REQ)])
  PKG_CHECK_MODULES(GTK, gtk+-x11-$with_gtk >= $GTK_REQ, HAVE_GTK=yes, HAVE_GTK=no)
  AC_SUBST(GTK_LIBS)
  AC_SUBST(GTK_CFLAGS)
fi
AM_CONDITIONAL(HAVE_GTK, test "x$HAVE_GTK" = "xyes")

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

dnl GTK is optional and used in examples
HAVE_GTK3=NO
HAVE_GTK3_WAYLAND=""
if test "x$BUILD_EXAMPLES" = "xyes"; then
  PKG_CHECK_MODULES(GTK3, gtk+-3.0 >= 3.4, HAVE_GTK3=yes, HAVE_GTK3=no)
  if test "x$HAVE_GTK3" = "xyes"; then
    HAVE_GTK=yes
    GTK_VERSION=`$PKG_CONFIG --variable=gtk_binary_version gtk+-3.0`
    AC_SUBST(GTK_VERSION)
    GTK_PREFIX=`$PKG_CONFIG --variable=prefix gdk-pixbuf-2.0`
    AC_SUBST(GTK_BASE_DIR)
    GTK3_TARGETS=`$PKG_CONFIG --variable=targets gtk+-3.0`
    case "$GTK3_TARGETS" in  *wayland*) HAVE_GTK3_WAYLAND="1" ;; esac
  fi
fi
AC_SUBST(GTK3_LIBS)
AC_SUBST(GTK3_CFLAGS)
AC_SUBST(HAVE_GTK3)
AM_CONDITIONAL(HAVE_GTK3, test "x$HAVE_GTK3" = "xyes")
AM_CONDITIONAL(HAVE_GTK3_WAYLAND, test "x$HAVE_GTK3_WAYLAND" = "x1")

dnl x11 is optional for librfb
HAVE_X11=NO
PKG_CHECK_MODULES(X11, x11, HAVE_X11=yes, HAVE_X11=no)
AC_SUBST(X11_LIBS)
AC_SUBST(X11_CFLAGS)
AC_SUBST(HAVE_X11)
AM_CONDITIONAL(HAVE_X11, test "x$HAVE_X11" = "xyes")
if test "x$HAVE_X11" = "xyes"; then
  AC_DEFINE(HAVE_X11, 1, [Define if you have X11 library])
fi

dnl exif (used on jifmux tests) ****
PKG_CHECK_MODULES(EXIF, libexif >= 0.6.16, HAVE_EXIF="yes", HAVE_EXIF="no")
AC_SUBST(EXIF_LIBS)
AC_SUBST(EXIF_CFLAGS)
AM_CONDITIONAL(USE_EXIF, test "x$HAVE_EXIF" = "xyes")

AG_GST_CHECK_FEATURE(IQA, [iqa], iqa , [
  PKG_CHECK_MODULES(DSSIM, dssim, [
    HAVE_DSSIM="yes"
    HAVE_IQA="yes"
  ], [
    HAVE_DSSIM="no"
    HAVE_IQA="no"
  ])

  if test "x$HAVE_DSSIM" = "xyes"; then
    AC_DEFINE(HAVE_DSSIM, 1, [Define if you have dssim library])
  fi
  AC_SUBST(DSSIM_LIBS)
  AC_SUBST(DSSIM_CFLAGS)
])
AM_CONDITIONAL(HAVE_DSSIM, test "x$HAVE_DSSIM" = "xyes")

dnl Orc
ORC_CHECK([0.4.17])

dnl set license and copyright notice
GST_LICENSE="LGPL"
AC_DEFINE_UNQUOTED(GST_LICENSE, "$GST_LICENSE", [GStreamer license])
AC_SUBST(GST_LICENSE)

dnl define LIBDIR so we can inform people where we live
AS_AC_EXPAND(LIBDIR, $libdir)
AC_DEFINE_UNQUOTED(LIBDIR, "$LIBDIR", [library dir])

dnl set location of plugin directory
AG_GST_SET_PLUGINDIR

dnl set release date/time
AG_GST_SET_PACKAGE_RELEASE_DATETIME_WITH_NANO([$PACKAGE_VERSION_NANO],
  ["${srcdir}/gst-plugins-bad.doap"],
  [$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR.$PACKAGE_VERSION_MICRO])

dnl If only building static libraries, define GST_STATIC_COMPILATION. This is
dnl needed only on Windows, but it doesn't hurt to have it everywhere.
if test x$enable_static = xyes -a x$enable_shared = xno; then
  GST_STATIC_CFLAGS="-DGST_STATIC_COMPILATION"
fi

AC_ARG_WITH([player-tests],
  AS_HELP_STRING([--with-player-tests],[Enable GstPlayer tests that need network access (default: no)]))
if test x$with_player_tests = xyes; then
  AC_PATH_PROG([WGET], [wget], no)
  if test x$WGET = xno; then
    AC_MSG_WARN([wget required for GstPlayer tests but not found - disabling])
    with_player_tests=no
  fi
fi
AM_CONDITIONAL(WITH_GST_PLAYER_TESTS, test "x$with_player_tests" = "xyes")

# set by AG_GST_PARSE_SUBSYSTEM_DISABLES above
dnl make sure it doesn't complain about unused variables if debugging is disabled
NO_WARNINGS=""
AG_GST_CHECK_GST_DEBUG_DISABLED([NO_WARNINGS="-Wno-unused"], [NO_WARNINGS=""])

dnl define an ERROR_CFLAGS Makefile variable
dnl -Waggregate-return - libexif returns aggregates
dnl -Wundef - Windows headers check _MSC_VER unconditionally
AG_GST_SET_ERROR_CFLAGS($FATAL_WARNINGS, [
    -Wmissing-declarations -Wmissing-prototypes -Wredundant-decls
    -Wwrite-strings -Wformat-security -Wold-style-definition
    -Winit-self -Wmissing-include-dirs -Waddress -Wno-multichar
    -Wnested-externs $NO_WARNINGS])

dnl define an ERROR_CXXFLAGS Makefile variable
AG_GST_SET_ERROR_CXXFLAGS($FATAL_WARNINGS, [
    -Wmissing-declarations -Wredundant-decls
    -Wwrite-strings -Wformat-nonliteral -Wformat-security
    -Winit-self -Wmissing-include-dirs -Waddress -Wno-multichar $NO_WARNINGS])

dnl define an ERROR_OBJCFLAGS Makefile variable
AG_GST_SET_ERROR_OBJCFLAGS($FATAL_WARNINGS, [
    -Wmissing-declarations -Wredundant-decls
    -Wwrite-strings -Wformat-nonliteral -Wformat-security
    -Winit-self -Wmissing-include-dirs -Wno-multichar $NO_WARNINGS])

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT($GST_GIT)

dnl used in examples
AG_GST_DEFAULT_ELEMENTS

dnl *** plug-ins to include ***

dnl these are all the gst plug-ins, compilable without additional libs
AG_GST_CHECK_PLUGIN(accurip)
AG_GST_CHECK_PLUGIN(adpcmdec)
AG_GST_CHECK_PLUGIN(adpcmenc)
AG_GST_CHECK_PLUGIN(aiff)
AG_GST_CHECK_PLUGIN(videoframe_audiolevel)
AG_GST_CHECK_PLUGIN(asfmux)
AG_GST_CHECK_PLUGIN(audiobuffersplit)
AG_GST_CHECK_PLUGIN(audiofxbad)
AG_GST_CHECK_PLUGIN(audiolatency)
AG_GST_CHECK_PLUGIN(audiomixmatrix)
AG_GST_CHECK_PLUGIN(compositor)
AG_GST_CHECK_PLUGIN(audiovisualizers)
AG_GST_CHECK_PLUGIN(autoconvert)
AG_GST_CHECK_PLUGIN(bayer)
AG_GST_CHECK_PLUGIN(camerabin2)
AG_GST_CHECK_PLUGIN(coloreffects)
AG_GST_CHECK_PLUGIN(debugutils)
AG_GST_CHECK_PLUGIN(dvbsuboverlay)
AG_GST_CHECK_PLUGIN(dvdspu)
AG_GST_CHECK_PLUGIN(faceoverlay)
AG_GST_CHECK_PLUGIN(festival)
AG_GST_CHECK_PLUGIN(fieldanalysis)
AG_GST_CHECK_PLUGIN(freeverb)
AG_GST_CHECK_PLUGIN(frei0r)
AG_GST_CHECK_PLUGIN(gaudieffects)
AG_GST_CHECK_PLUGIN(geometrictransform)
AG_GST_CHECK_PLUGIN(gdp)
AG_GST_CHECK_PLUGIN(id3tag)
AG_GST_CHECK_PLUGIN(inter)
AG_GST_CHECK_PLUGIN(interlace)
AG_GST_CHECK_PLUGIN(ivfparse)
AG_GST_CHECK_PLUGIN(ivtc)
AG_GST_CHECK_PLUGIN(jp2kdecimator)
AG_GST_CHECK_PLUGIN(jpegformat)
AG_GST_CHECK_PLUGIN(librfb)
AG_GST_CHECK_PLUGIN(midi)
AG_GST_CHECK_PLUGIN(mpegdemux)
AG_GST_CHECK_PLUGIN(mpegtsdemux)
AG_GST_CHECK_PLUGIN(mpegtsmux)
AG_GST_CHECK_PLUGIN(mpegpsmux)
AG_GST_CHECK_PLUGIN(mxf)
AG_GST_CHECK_PLUGIN(netsim)
AG_GST_CHECK_PLUGIN(onvif)
AG_GST_CHECK_PLUGIN(pcapparse)
AG_GST_CHECK_PLUGIN(pnm)
AG_GST_CHECK_PLUGIN(proxy)
AG_GST_CHECK_PLUGIN(rawparse)
AG_GST_CHECK_PLUGIN(removesilence)
AG_GST_CHECK_PLUGIN(sdp)
AG_GST_CHECK_PLUGIN(segmentclip)
AG_GST_CHECK_PLUGIN(siren)
AG_GST_CHECK_PLUGIN(smooth)
AG_GST_CHECK_PLUGIN(speed)
AG_GST_CHECK_PLUGIN(subenc)
AG_GST_CHECK_PLUGIN(stereo)
AG_GST_CHECK_PLUGIN(timecode)
AG_GST_CHECK_PLUGIN(videofilters)
AG_GST_CHECK_PLUGIN(videoparsers)
AG_GST_CHECK_PLUGIN(videosignal)
AG_GST_CHECK_PLUGIN(vmnc)
AG_GST_CHECK_PLUGIN(y4m)
AG_GST_CHECK_PLUGIN(yadif)

dnl *** plug-ins to exclude ***

AC_CHECK_HEADER(AVFoundation/AVFoundation.h, HAVE_AVFOUNDATION="yes", HAVE_AVFOUNDATION="no", [-])
AC_CHECK_HEADER(MobileCoreServices/MobileCoreServices.h, HAVE_IOS="yes", HAVE_IOS="no", [-])
AC_CHECK_HEADER(VideoToolbox/VideoToolbox.h, HAVE_VIDEOTOOLBOX="yes", HAVE_VIDEOTOOLBOX="no", [-])

if test "x$HAVE_VIDEOTOOLBOX" = "xyes"; then
  old_LIBS=$LIBS
  LIBS="$LIBS -framework VideoToolbox"
  AC_CHECK_FUNC(VTCompressionSessionPrepareToEncodeFrames, [
    AC_DEFINE(HAVE_VIDEOTOOLBOX_10_9_6, 1, [Define if building with VideoToolbox >= 10.9.6])
  ], [
  ])
  LIBS=$old_LIBS
fi

AM_CONDITIONAL(HAVE_AVFOUNDATION, test "x$HAVE_AVFOUNDATION" = "xyes")
if test "x$HAVE_AVFOUNDATION" = "xyes"; then
  AC_DEFINE(HAVE_AVFOUNDATION, 1, [Define if building with AVFoundation])
fi

AM_CONDITIONAL(HAVE_IOS, test "x$HAVE_IOS" = "xyes")
if test "x$HAVE_IOS" = "xyes"; then
  AC_DEFINE(HAVE_IOS, 1, [Define if building for Apple iOS])
fi

AM_CONDITIONAL(HAVE_VIDEOTOOLBOX, test "x$HAVE_VIDEOTOOLBOX" = "xyes")
if test "x$HAVE_VIDEOTOOLBOX" = "xyes"; then
  AC_DEFINE(HAVE_VIDEOTOOLBOX, 1, [Define if building with VideoToolbox])
fi

dnl disable gst plugins we might not be able to build on this
dnl platform: (ugly but minimally invasive)
dnl FIXME: maybe move to sys, or make work with winsock2
AC_CHECK_HEADERS([sys/socket.h], HAVE_SYS_SOCKET_H=yes)
AC_CHECK_HEADERS([winsock2.h], HAVE_WINSOCK2_H=yes)

if test "x$HAVE_WINSOCK2_H" = "xyes"; then
  WINSOCK2_LIBS="-lws2_32"
  AC_SUBST(WINSOCK2_LIBS)
fi

dnl Vulkan
PKG_CHECK_MODULES(XCB, xcb >= 1.10, GST_VULKAN_HAVE_WINDOW_XCB=1, GST_VULKAN_HAVE_WINDOW_XCB=0)
AM_CONDITIONAL(USE_XCB, test "x$GST_VULKAN_HAVE_WINDOW_XCB" = "x1")

PKG_CHECK_MODULES(VULKAN_WAYLAND, wayland-client >= 1.4, GST_VULKAN_HAVE_WINDOW_WAYLAND=1, GST_VULKAN_HAVE_WINDOW_WAYLAND=0)
AM_CONDITIONAL(USE_WAYLAND, test "x$GST_VULKAN_HAVE_WINDOW_WAYLAND" = "x1")
VULKAN_CONFIG_DEFINES="
#define GST_VULKAN_HAVE_WINDOW_XCB $GST_VULKAN_HAVE_WINDOW_XCB
#define GST_VULKAN_HAVE_WINDOW_WAYLAND $GST_VULKAN_HAVE_WINDOW_WAYLAND"

AC_CONFIG_COMMANDS([ext/vulkan/vkconfig.h], [
	outfile=vkconfig.h-tmp
	cat > $outfile <<\_______EOF
/* vkconfig.h
 *
 * This is a generated file.  Please modify `configure.ac'
 */

#ifndef __GST_VULKAN_CONFIG_H__
#define __GST_VULKAN_CONFIG_H__

#include <gst/gst.h>

G_BEGIN_DECLS

_______EOF

	cat >>$outfile <<_______EOF
$vulkan_config_defines
_______EOF

	cat >>$outfile <<_______EOF

G_END_DECLS

#endif  /* __GST_VULKAN_CONFIG_H__ */
_______EOF


	if cmp -s $outfile ext/vulkan/vkconfig.h; then
          AC_MSG_NOTICE([ext/vulkan/vkconfig.h is unchanged])
	  rm -f $outfile
	else
	  mv $outfile ext/vulkan/vkconfig.h
	fi
],[
vulkan_config_defines='$VULKAN_CONFIG_DEFINES'
])

dnl *** sys plug-ins ***

dnl DirectSound
translit(dnm, m, l) AM_CONDITIONAL(USE_DIRECTSOUND, true)
AG_GST_CHECK_FEATURE(DIRECTSOUND, [DirectSound], directsoundsrc, [
  HAVE_DIRECTSOUND="no"
  save_CFLAGS="$CFLAGS"
  save_LDFLAGS="$LDFLAGS"
  save_LIBS="$LIBS"
  CFLAGS="$CFLAGS $DIRECTX_CFLAGS"
  LDFLAGS="$LDFLAGS $DIRECTX_LDFLAGS"
  LIBS="$LIBS -lwinmm -ldsound -ldxerr9 -luser32"
  AC_MSG_CHECKING(for DirectSound LDFLAGS)
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <windows.h>
#include <dxerr9.h>
#include <dsound.h>
#include <mmsystem.h>
]], [[
  DXGetErrorString9 (0);
  DirectSoundCreate(NULL, NULL, NULL);
]])],
    [HAVE_DIRECTSOUND="yes"],
    [HAVE_DIRECTSOUND="no"])
  AC_MSG_RESULT($HAVE_DIRECTSOUND)
  CFLAGS=$save_CFLAGS
  LDFLAGS=$save_LDFLAGS
  LIBS=$save_LIBS

  if test "x$HAVE_DIRECTSOUND" = "xyes";  then
    dnl this is much more than we want
    DIRECTSOUND_LIBS="-lwinmm -ldsound -ldxerr9 -luser32"
    AC_SUBST(DIRECTX_CFLAGS)
    AC_SUBST(DIRECTX_LDFLAGS)
    AC_SUBST(DIRECTSOUND_LIBS)
  fi
  AC_SUBST(HAVE_DIRECTSOUND)
])

dnl WASAPI
translit(dnm, m, l) AM_CONDITIONAL(USE_WASAPI, true)
AG_GST_CHECK_FEATURE(WASAPI, [WASAPI plug-in], wasapi, [
  HAVE_WASAPI="no"
  save_CFLAGS="$CFLAGS"
  save_LDFLAGS="$LDFLAGS"
  save_LIBS="$LIBS"
  CFLAGS="$CFLAGS $DIRECTX_CFLAGS"
  LDFLAGS="$LDFLAGS $DIRECTX_LDFLAGS"
  LIBS="$LIBS -lole32 -lwinmm -lksuser"
  AC_MSG_CHECKING(for WASAPI LDFLAGS)
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <windows.h>
#include <stdio.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
]], [[
int main ()
{
    HRESULT hr = S_OK;

    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        printf("CoInitialize failed: hr = 0x%08x", hr);
        return __LINE__;
    }

	int result = 0;
    // result = do_everything(argc, argv);

    CoUninitialize();
    return result;
}
]])],
    [HAVE_WASAPI="yes"],
    [HAVE_WASAPI="no"])
  AC_MSG_RESULT($HAVE_WASAPI)
  CFLAGS=$save_CFLAGS
  LDFLAGS=$save_LDFLAGS
  LIBS=$save_LIBS

  if test "x$HAVE_WASAPI" = "xyes";  then
    WASAPI_LIBS="-lole32 -lwinmm -lksuser"
    AC_SUBST(WASAPI_LIBS)
  fi
  AC_SUBST(HAVE_WASAPI)
])

dnl Direct3D
translit(dnm, m, l) AM_CONDITIONAL(USE_DIRECT3D, true)
AG_GST_CHECK_FEATURE(DIRECT3D, [Direct3D plug-in], direct3dsink, [
  HAVE_DIRECT3D="no"
  save_CFLAGS="$CFLAGS"
  save_LDFLAGS="$LDFLAGS"
  save_LIBS="$LIBS"
  CFLAGS="$CFLAGS $DIRECTX_CFLAGS"
  LDFLAGS="$LDFLAGS $DIRECTX_LDFLAGS"
  LIBS="$LIBS -ld3d9 -lgdi32"
  AC_MSG_CHECKING(for Direct3D LDFLAGS)
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <windows.h>
#include <d3d9.h>
]], [[
  Direct3DCreate9(D3D_SDK_VERSION);
]])],
    [HAVE_DIRECT3D="yes"],
    [HAVE_DIRECT3D="no"])
  AC_MSG_RESULT($HAVE_DIRECT3D)
  CFLAGS=$save_CFLAGS
  LDFLAGS=$save_LDFLAGS
  LIBS=$save_LIBS

  if test "x$HAVE_DIRECT3D" = "xyes";  then
    DIRECT3D_LIBS="-ld3d9 -lgdi32"
    AC_SUBST(DIRECT3D_LIBS)
  fi
  AC_SUBST(HAVE_DIRECT3D)
])

dnl winscreencap
translit(dnm, m, l) AM_CONDITIONAL(USE_WINSCREENCAP, true)
AG_GST_CHECK_FEATURE(WINSCREENCAP, [winscreencap plug-in], winscreencap, [
  HAVE_WINSCREENCAP="no"
  save_CFLAGS="$CFLAGS"
  save_LDFLAGS="$LDFLAGS"
  save_LIBS="$LIBS"
  CFLAGS="$CFLAGS $DIRECTX_CFLAGS"
  LDFLAGS="$LDFLAGS $DIRECTX_LDFLAGS"
  LIBS="$LIBS -ld3d9 -lgdi32"
  AC_MSG_CHECKING(for Direct3D LDFLAGS)
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <windows.h>
#include <d3d9.h>
]], [[
  Direct3DCreate9(D3D_SDK_VERSION);
]])],
    [HAVE_WINSCREENCAP="yes"],
    [HAVE_WINSCREENCAP="no"])
  AC_MSG_RESULT($HAVE_WINSCREENCAP)
  CFLAGS=$save_CFLAGS
  LDFLAGS=$save_LDFLAGS
  LIBS=$save_LIBS

  if test "x$HAVE_WINSCREENCAP" = "xyes";  then
    WINSCREENCAP_LIBS="-ld3d9 -lgdi32"
    AC_SUBST(WINSCREENCAP_LIBS)
  fi
  AC_SUBST(HAVE_WINSCREENCAP)
])

dnl winks
translit(dnm, m, l) AM_CONDITIONAL(USE_WINKS, true)
AG_GST_CHECK_FEATURE(WINKS, [Winks plug-in], winks, [
  HAVE_WINKS="no"
  save_CFLAGS="$CFLAGS"
  save_LDFLAGS="$LDFLAGS"
  save_LIBS="$LIBS"
  CFLAGS="$CFLAGS $DIRECTX_CFLAGS"
  LDFLAGS="$LDFLAGS $DIRECTX_LDFLAGS"
  LIBS="$LIBS -lole32 -luuid -lstrmiids"
  AC_MSG_CHECKING(for Winks LDFLAGS)
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <windows.h>
#include <strmif.h>
#include <uuids.h>
]], [[
int main () {
  IGraphBuilder *pGraph = NULL;
  HRESULT hr = CoInitialize(NULL);
  hr = CoCreateInstance(&CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER,
                        &IID_IGraphBuilder, (void **)&pGraph);

  return 0;
}
]])],
    [HAVE_WINKS="yes"],
    [HAVE_WINKS="no"])
  AC_MSG_RESULT($HAVE_WINKS)
  CFLAGS=$save_CFLAGS
  LDFLAGS=$save_LDFLAGS
  LIBS=$save_LIBS

  if test "x$HAVE_WINKS" = "xyes";  then
    dnl this is much more than we want
    WINKS_LIBS="-ldxguid -lole32 -luuid -lstrmiids -lksuser -lsetupapi"
    AC_SUBST(DIRECTX_CFLAGS)
    AC_SUBST(DIRECTX_LDFLAGS)
    AC_SUBST(WINKS_LIBS)
  fi
  AC_SUBST(HAVE_WINKS)
])

dnl *** Android Media ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ANDROID_MEDIA, true)
HAVE_ANDROID_MEDIA="no"
AG_GST_CHECK_FEATURE(ANDROID_MEDIA, [Android Media], androidmedia, [
  case "$host" in
    *android*)
      AC_CHECK_HEADER(jni.h, HAVE_ANDROID_MEDIA="yes", HAVE_ANDROID_MEDIA="no")
      ;;
    *)
      HAVE_ANDROID_MEDIA="no"
      ;;
  esac
])

dnl *** AppleMedia (OS X and iOS) ***
translit(dnm, m, l) AM_CONDITIONAL(USE_APPLE_MEDIA, true)
HAVE_APPLE_MEDIA="no"
dnl SystemConfiguration.h is common to OS X and iOS, so we check for that
AG_GST_CHECK_FEATURE(APPLE_MEDIA, [Apple video], applemedia, [
  AC_CHECK_HEADER(SystemConfiguration/SystemConfiguration.h, HAVE_APPLE_MEDIA="yes", HAVE_APPLE_MEDIA="no")
  dnl OSX 10.7 and earlier lack VideoToolbox/VideoToolbox.h
  if test "x$HAVE_IOS" = "xno"; then
    AC_CHECK_HEADER(VideoToolbox/VideoToolbox.h,, HAVE_APPLE_MEDIA="no")
  fi
])

dnl in case header SystemConfiguration/SystemConfiguration.h is found on other platforms
case "$host" in
  *-*darwin*)
    ;;
  *)
    HAVE_APPLE_MEDIA="no"
    ;;
esac

dnl *** bluez ***
translit(dnm, m, l) AM_CONDITIONAL(USE_BLUEZ, true)
AG_GST_CHECK_FEATURE(BLUEZ, [Bluez], bluez, [
  PKG_CHECK_MODULES([BLUEZ5], [bluez >= 5.0], [
    PKG_CHECK_MODULES([GIO_UNIX], gio-unix-2.0 > 2.24, [
      AC_CHECK_PROG([GDBUS_CODEGEN],[gdbus-codegen],[gdbus-codegen])
      HAVE_BLUEZ=yes
      AC_DEFINE(HAVE_BLUEZ5,[1],[Bluez5 detected])
    ], [
      HAVE_BLUEZ=no
    ])
  ], [
    HAVE_BLUEZ=no
  ])
])

dnl *** OS/X AVCVideoServices ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AVC, true)
HAVE_AVC="no"
AG_GST_CHECK_FEATURE(AVC, [AVC Video Services], avcsrc, [
  AC_LANG_PUSH([C++])
  save_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS -framework AVCVideoServices -framework CoreFoundation"
  save_LIBS="$LIBS"
  LIBS="$LIBS -framework AVCVideoServices -framework CoreFoundation"
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[]], [[]])],[HAVE_AVC=yes],[HAVE_AVC=no])
  LIBS=$save_LIBS
  CPPFLAGS=$save_CPPFLAGS
  AC_LANG_POP([C++])
])
dnl in case header AVCVideoServices/AVCVideoServices.h is found on other platforms
case "$host" in
  *-*darwin*)
    ;;
  *)
    HAVE_AVC="no"
    ;;
esac

dnl check for shm_open (for shm plugin)
translit(dnm, m, l) AM_CONDITIONAL(USE_SHM, true)
AG_GST_CHECK_FEATURE(SHM, [POSIX shared memory source and sink], shm, [
    if test "x$HAVE_SYS_SOCKET_H" = "xyes"; then
        case $host in
        *-darwin* | *-macos10*)
            AC_DEFINE(HAVE_OSX,[1],[Apple Mac OS X operating system detected])
            HAVE_SHM=yes
            ;;
        *-*-OpenBSD*)
            HAVE_SHM=yes
            ;;
        *)
            AC_CHECK_LIB([rt], [shm_open],
                AC_CHECK_DECL([MSG_NOSIGNAL], HAVE_SHM=yes, HAVE_SHM=no, [
                #include <sys/socket.h>
  	            ]),
                HAVE_SHM=no)
            AC_SUBST(SHM_LIBS, "-lrt")
            ;;
        esac
    else
        HAVE_SHM=no
    fi
])

dnl check for unix sockets (ipcpipeline plugin)
translit(dnm, m, l) AM_CONDITIONAL(USE_IPCPIPELINE, true)
AG_GST_CHECK_FEATURE(IPCPIPELINE, [Unix sockets], ipcpipeline, [
    if test "x$HAVE_SYS_SOCKET_H" = "xyes"; then
        AC_CHECK_FUNC(pipe, [
          AC_CHECK_FUNC(socketpair, [HAVE_IPCPIPELINE=yes], [HAVE_IPCPIPELINE=no])
          ], [
          HAVE_IPCPIPELINE=no
        ])
    else
        HAVE_IPCPIPELINE=no
    fi
])

dnl check for Video CD
translit(dnm, m, l) AM_CONDITIONAL(USE_VCD, true)
AG_GST_CHECK_FEATURE(VCD, [Video CD], vcdsrc, [
  AC_CHECK_HEADER(linux/cdrom.h, HAVE_VCD="yes", HAVE_VCD="no")
])

dnl check for OpenSL ES
translit(dnm, m, l) AM_CONDITIONAL(USE_OPENSLES, true)
AG_GST_CHECK_FEATURE(OPENSLES, [OpenSL ES], opensl, [
  AC_CHECK_HEADER(SLES/OpenSLES.h, HAVE_OPENSLES="yes", HAVE_OPENSLES="no")
])

dnl *** UVC H264 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_UVCH264, true)
AG_GST_CHECK_FEATURE(UVCH264, [UVC H264], uvch264, [
  AC_CHECK_HEADER(linux/uvcvideo.h, HAVE_UVCVIDEO_H=yes, HAVE_UVCVIDEO_H=no)
  PKG_CHECK_MODULES(G_UDEV, gudev-1.0 , [
          AC_DEFINE([HAVE_GUDEV], 1, [Define if gudev is installed])
          HAVE_GUDEV="yes" ],
          [HAVE_GUDEV="no"])
  PKG_CHECK_MODULES(LIBUSB, libusb-1.0 , [
          AC_DEFINE([HAVE_LIBUSB], 1, [Define if libusb 1.x is installed])
          HAVE_LIBUSB="yes" ],
          [HAVE_LIBUSB="no"])
  if test "x$HAVE_UVCVIDEO_H" = "xyes" && \
     test "x$HAVE_GUDEV" = "xyes" && \
     test "x$HAVE_LIBUSB" = "xyes"; then
    HAVE_UVCH264=yes
  else
    HAVE_UVCH264=no
  fi
])
AC_SUBST(LIBUDEV_CFLAGS)
AC_SUBST(LIBUDEV_LIBS)
AC_SUBST(LIBUSB_CFLAGS)
AC_SUBST(LIBUSB_LIBS)

dnl *** CUDA ***
translit(dnm, m, l) AM_CONDITIONAL(USE_CUDA, true)
AG_GST_CHECK_FEATURE(CUDA, [NVIDIA CUDA API],, [
  AC_ARG_WITH([cuda-prefix],
          AS_HELP_STRING([--with-cuda-prefix],
          [Use the provided prefix for detecting the cuda installation]),
          [AS_IF([test "x$with_cuda_prefix" != "x"],
                 [CUDA_PREFIX="$with_cuda_prefix"])],
          [CUDA_PREFIX=""])

  HAVE_CUDA="yes"
  if test "x$CUDA_PREFIX" != "x"; then
    dnl only override if not already set
    if test "x$CUDA_CFLAGS" = "x" -a "x$CUDA_LIBS" = "x"; then
      dnl this is an educated guess, user can always override these
      CUDA_CFLAGS="-I$CUDA_PREFIX/include"
      CUDA_LIBS="-L$CUDA_PREFIX/lib -L$CUDA_PREFIX/lib64 -L$CUDA_PREFIX/lib/stubs -L$CUDA_PREFIX/lib64/stubs -lcuda -lcudart"
    fi
  else
    PKG_CHECK_MODULES([CUDA], [cuda-8.0 cudart-8.0],, [
      PKG_CHECK_MODULES([CUDA], [cuda-7.5 cudart-7.5],, [
        PKG_CHECK_MODULES([CUDA], [cuda-7.0 cudart-7.0],, [
          PKG_CHECK_MODULES([CUDA], [cuda-6.5 cudart-6.5],, [
            AC_MSG_WARN([Could not find cuda headers/libraries])])])])])
  fi

  HAVE_CUDA_H=no
  HAVE_CUDART_H=no
  save_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CUDA_CFLAGS $save_CPPFLAGS "
  AC_CHECK_HEADER([cuda.h], [HAVE_CUDA_H=yes],
      AC_MSG_WARN([Could not find cuda.h]))
  AC_CHECK_HEADER([cuda_runtime_api.h], [HAVE_CUDART_H=yes],
      AC_MSG_WARN([Could not find cuda_runtime_api.h]))
  CPPFLAGS=$save_CPPFLAGS

  dnl libcuda and libcudart libraries
  save_LIBS="$LIBS"
  LIBS="$CUDA_LIBS $save_LIBS"
  HAVE_CUDART_LIB="no"
  AC_CHECK_LIB(cudart,cudaGetErrorString,[HAVE_CUDART_LIB="yes"], [
      AC_MSG_WARN([Could not find cudart library])])
  HAVE_CUDA_LIB="no"
  AC_CHECK_LIB(cuda,cuInit,[HAVE_CUDA_LIB="yes"], [
      AC_MSG_WARN([Could not find cuda library])])
  LIBS="$save_LIBS"
])

dnl *** NVDEC ***
translit(dnm, m, l) AM_CONDITIONAL(USE_NVDEC, true)
AG_GST_CHECK_FEATURE(NVDEC, [nvdec], nvdec, [
  HAVE_NVCUVID_H=no
  save_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CUDA_CFLAGS $save_CPPFLAGS"
  AC_CHECK_HEADER([nvcuvid.h], [HAVE_NVCUVID_H=yes],
      AC_MSG_WARN([Could not find nvcuvid.h]))
  CPPFLAGS=$save_CPPFLAGS

  HAVE_NVCUVID=no
  save_LIBS="$LIBS"
  LIBS="$CUDA_LIBS $save_LIBS"
  AC_CHECK_LIB(nvcuvid, cuvidCtxLock, [HAVE_NVCUVID=yes],
      AC_MSG_WARN([Could not find library nvcuvid]))
  LIBS="$save_LIBS"

  if test "x$HAVE_NVCUVID_H" = "xyes" -a "x$HAVE_NVCUVID" = "xyes"; then
    HAVE_NVDEC=yes
  else
    HAVE_NVDEC=no
  fi
])

dnl *** NVENC ***
translit(dnm, m, l) AM_CONDITIONAL(USE_NVENC, true)
AG_GST_CHECK_FEATURE(NVENC, [NVIDIA Encode API], nvenc, [
  dnl nvEncodeAPI.h header
  HAVE_NVENCODEAPI_H=no
  AC_ARG_VAR(NVENCODE_CFLAGS, [C compiler flags for NvEncodeAPI.h])
  save_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$NVENCODE_CFLAGS $save_CPPFLAGS"
  AC_CHECK_HEADER([nvEncodeAPI.h], [
      AC_PREPROC_IFELSE([AC_LANG_SOURCE([[#include <nvEncodeAPI.h>
          #if NVENCAPI_MAJOR_VERSION < 5
          #error "Need nvEncodeAPI.h >= 5.0"
          #endif
          ]])], [
          HAVE_NVENCODEAPI_H=yes
          ], [
          AC_MSG_WARN([nvEncodeAPI.h must be >= 5.0])
          ])
      ],
      AC_MSG_WARN([Could not find nvEncodeAPI.h]))
  AC_SUBST(NVENCODE_CFLAGS)
  CPPFLAGS="$save_CPPFLAGS"

  dnl libnvnidia-encode library
  HAVE_NVENCODE_LIB=no
  AC_ARG_VAR(NVENCODE_LIBS, [linker flags for nvidia-encode])
  saved_LIBS="$LIBS"
  LIBS="$NVENCODE_LIBS $saved_LIBS"
  AC_CHECK_LIB(nvidia-encode, NvEncodeAPICreateInstance, [HAVE_NVENCODE_LIB="yes"],
      AC_MSG_WARN([Could not find library nvidia-encode]))
  NVENCODE_LIBS="$NVENCODE_LIBS -lnvidia-encode"
  AC_SUBST(NVENCODE_LIBS)
  LIBS="$saved_LIBS"

  USE_NVENC_GST_GL=no
  if test "x$HAVE_CUDA_H" = "xyes" \
      -a "x$HAVE_CUDART_H" = "xyes" \
      -a "x$HAVE_CUDA_LIB" = "xyes" \
      -a "x$HAVE_CUDART_LIB" = "xyes" \
      -a "x$HAVE_NVENCODEAPI_H" = "xyes" \
      -a "x$HAVE_NVENCODE_LIB" = "xyes"; then
    HAVE_NVENC="yes"
    if test x"$USE_OPENGL" = x"yes"; then
      dnl cuda-gl interop header
      save_CPPFLAGS="$CPPFLAGS"
      CPPFLAGS="$save_CPPFLAGS $CUDA_CFLAGS"
      AC_CHECK_HEADER([cuda_gl_interop.h], [
        USE_NVENC_GST_GL="yes"
        AC_DEFINE(HAVE_NVENC_GST_GL, [1] , [NVENC GStreamer OpenGL support available])
        ])
      CPPFLAGS="$save_CPPFLAGS"
    fi
  else
    HAVE_NVENC="no"
  fi
])
AM_CONDITIONAL(USE_NVENC_GST_GL, test "x$USE_NVENC_GST_GL" = "xyes")

dnl check for tinyalsa
translit(dnm, m, l) AM_CONDITIONAL(USE_TINYALSA, true)
AG_GST_CHECK_FEATURE(TINYALSA, [tinyalsa], tinyalsa, [
  AC_CHECK_HEADER(tinyalsa/asoundlib.h, HAVE_TINYALSA="yes", HAVE_TINYALSA="no")
])

dnl check for intel mediasdk
translit(dnm, m, l) AM_CONDITIONAL(USE_MSDK, true)
AG_GST_CHECK_FEATURE(MSDK, [Intel MediaSDK], msdk, [
  PKG_CHECK_MODULES(G_UDEV, gudev-1.0 , [
          AC_DEFINE([HAVE_GUDEV], 1, [Define if gudev is installed])
          HAVE_GUDEV="yes" ],
          [HAVE_GUDEV="no"])
  PKG_CHECK_MODULES(LIBMFX, libmfx,
  [
    AC_DEFINE(HAVE_LIBMFX, 1, [Define if mfx_dispatcher is available])
    HAVE_MSDK="yes"
  ], [
    AC_ARG_WITH([msdk-prefix],
            AS_HELP_STRING([--with-msdk-prefix],
            [Use the provided prefix for detecting the Intel MediaSDK]),
            [AS_IF([test "x$with_msdk_prefix" != "x"],
                   [MSDK_PREFIX="$with_msdk_prefix"])],
            [AS_IF([test "x$MFX_HOME" != "x"],
                   [MSDK_PREFIX="$MFX_HOME"],
                   [MSDK_PREFIX="/opt/intel/media"])])
    MSDK_CFLAGS="-I$MSDK_PREFIX/include"
    MSDK_LIBS="-L$MSDK_PREFIX/lib/lin_x64 -lmfx -ldl"
    AC_SUBST(MSDK_CFLAGS)
    AC_SUBST(MSDK_LIBS)

    save_CPPFLAGS="$CPPFLAGS"
    CPPFLAGS="$MSDK_CFLAGS $save_CPPFLAGS"
    AC_CHECK_HEADER(mfxdefs.h, HAVE_MFXDEFS_H="yes", HAVE_MFXDEFS_H="no")
    CPPFLAGS="$save_CPPFLAGS"

    save_LIBS="$LIBS"
    LIBS="$MSDK_LIBS $LIBS"
    AC_LANG_PUSH([C++])
    AC_CHECK_LIB(mfx,MFXInit,HAVE_MFX_LIB="yes",HAVE_MFX_LIB="no")
    AC_LANG_POP([C++])
    LIBS="$save_LIBS"

    if test "x$HAVE_MFXDEFS_H" = "xyes" \
        -a "x$HAVE_MFX_LIB" = "xyes"; then
      HAVE_MSDK="yes"
    else
      HAVE_MSDK="no"
    fi
  ])

  PKG_CHECK_MODULES(LIBVA_DRM, libva-drm, HAVE_LIBVA_DRM="yes", HAVE_LIBVA_DRM="no")

  if test "x$HAVE_GUDEV" = "xyes" \
     -a  "x$HAVE_MSDK" = "xyes" \
     -a "x$HAVE_LIBVA_DRM" = "xyes"; then
    HAVE_MSDK="yes"
  else
    HAVE_MSDK="no"
  fi
])
AM_CONDITIONAL(USE_MSDK_LIBVA,
    test "x$HAVE_MSDK" = "xyes" -a "x$HAVE_LIBVA_DRM" = "xyes")

dnl *** ext plug-ins ***
dnl keep this list sorted alphabetically !

if test "x$BUILD_EXTERNAL" = "xyes"; then

dnl *** assrender ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ASSRENDER, true)
AG_GST_CHECK_FEATURE(ASSRENDER, [ASS/SSA renderer], assrender, [
  PKG_CHECK_MODULES(ASSRENDER, libass >= 0.10.2, [
    HAVE_ASSRENDER="yes" ], [
    HAVE_ASSRENDER="no"
  ])
])
AC_SUBST(ASSRENDER_CFLAGS)
AC_SUBST(ASSRENDER_LIBS)

dnl *** AOM ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AOM, true)
AG_GST_CHECK_FEATURE(AOM, [AV1 encoder/decoder], aom, [
  AG_GST_PKG_CHECK_MODULES(AOM, aom)
])

dnl *** vo-amrwbenc ***
translit(dnm, m, l) AM_CONDITIONAL(USE_VOAMRWBENC, true)
AG_GST_CHECK_FEATURE(VOAMRWBENC, [vo-amrwbenc library], vo-amrwbenc, [
  AG_GST_PKG_CHECK_MODULES(VOAMRWBENC, vo-amrwbenc >= 0.1.0)
])

dnl *** vo-aacenc ***
translit(dnm, m, l) AM_CONDITIONAL(USE_VOAACENC, true)
AG_GST_CHECK_FEATURE(VOAACENC, [vo-aacenc library], vo-aacenc, [
  AG_GST_PKG_CHECK_MODULES(VOAACENC, vo-aacenc >= 0.1.0)
])

dnl *** bs2b ***
translit(dnm, m, l) AM_CONDITIONAL(USE_BS2B, true)
AG_GST_CHECK_FEATURE(BS2B, [bs2b], bs2b, [
  PKG_CHECK_MODULES(BS2B, libbs2b >= 3.1.0, HAVE_BS2B="yes", [
    HAVE_BS2B="no"
  ])
  AC_SUBST(BS2B_CFLAGS)
  AC_SUBST(BS2B_LIBS)
])

dnl *** BZ2 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_BZ2, true)
AG_GST_CHECK_FEATURE(BZ2, [bz2 library], bz2, [
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

#dnl *** cairo ***
#translit(dnm, m, l) AM_CONDITIONAL(USE_CAIRO, true)
#AG_GST_CHECK_FEATURE(CAIRO, [cairo plug-in], cairo, [
#  PKG_CHECK_MODULES(CAIRO, cairo >= 1.0 glitz-glx, HAVE_CAIRO=yes, [
#    HAVE_CAIRO=no
#  ])
#  AC_SUBST(CAIRO_CFLAGS)
#  AC_SUBST(CAIRO_LIBS)
#])

dnl *** chromaprint ***
translit(dnm, m, l) AM_CONDITIONAL(USE_CHROMAPRINT, true)
AG_GST_CHECK_FEATURE(CHROMAPRINT, [chromaprint], chromaprint, [
  PKG_CHECK_MODULES(CHROMAPRINT, libchromaprint, HAVE_CHROMAPRINT="yes", [
    HAVE_CHROMAPRINT="no"
  ])
  AC_SUBST(CHROMAPRINT_CFLAGS)
  AC_SUBST(CHROMAPRINT_LIBS)
])

dnl *** Curl ***
translit(dnm, m, l) AM_CONDITIONAL(USE_CURL, true)
AG_GST_CHECK_FEATURE(CURL, [Curl plugin], curl, [
  PKG_CHECK_MODULES(CURL, libcurl >= 7.35.0, [
    HAVE_CURL="yes"
    AC_CHECK_HEADERS([unistd.h sys/socket.h sys/types.h netinet/in.h netinet/tcp.h fcntl.h], [ ], [
      AC_CHECK_HEADERS([unistd.h sys/types.h winsock2.h ws2tcpip.h fcntl.h], [ ], [HAVE_CURL="no"])
    ])
    AC_CHECK_HEADERS([netinet/ip.h], [], [HAVE_CURL="no"],
    [[#ifdef HAVE_SYS_TYPES_H
     # include <sys/types.h>
     #endif
     #ifdef HAVE_NETINET_IN_H
     # include <netinet/in.h>
     #endif
    ]])
    AC_MSG_CHECKING([for socket support needed by curlsink])
    AC_MSG_RESULT($HAVE_CURL)
  ], [
    HAVE_CURL="no"
  ])
  AC_SUBST(CURL_CFLAGS)
  AC_SUBST(CURL_LIBS)
  PKG_CHECK_MODULES(SSH2, libssh2 >= 1.4.3, [
    HAVE_SSH2="yes"
    AC_DEFINE(HAVE_SSH2, 1, [Define if libssh2 is available])
  ], [
    HAVE_SSH2="no"
  ])
  AM_CONDITIONAL(USE_SSH2, test "x$HAVE_SSH2" = "xyes")
  AC_SUBST(SSH2_CFLAGS)
  AC_SUBST(SSH2_LIBS)
],,,[AM_CONDITIONAL(USE_SSH2, false)])

dnl **** DASH ****
translit(dnm, m, l) AM_CONDITIONAL(USE_DASH, true)
AG_GST_CHECK_FEATURE(DASH, [DASH plug-in], dash, [
  PKG_CHECK_MODULES(LIBXML2, libxml-2.0 >= 2.8, [HAVE_LIBXML2="yes"
    HAVE_DASH="yes"], [HAVE_LIBXML2="no"
    HAVE_DASH="no"])
  AC_SUBST(LIBXML2_CFLAGS)
  AC_SUBST(LIBXML2_LIBS)
])

dnl *** dc1394  ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DC1394, true)
AG_GST_CHECK_FEATURE(DC1394, [libdc1394], dc1394, [
   PKG_CHECK_MODULES(LIBDC1394, libdc1394-2 >= 2.0.0, [
   	if test -z "`$PKG_CONFIG --modversion libdc1394-2 | grep 2.0.0-rc`"; then
		HAVE_DC1394="yes"
	else
		HAVE_DC1394="no"
	fi
   ], [
        HAVE_DC1394="no"
	AC_MSG_RESULT(no)
   ])
   AC_SUBST(LIBDC1394_CFLAGS)
   AC_SUBST(LIBDC1394_LIBS)
])

dnl *** decklink  ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DECKLINK, true)
AG_GST_CHECK_FEATURE(DECKLINK, [decklink], decklink, [
   HAVE_DECKLINK=no
   case "$host" in
     *android*)
       HAVE_DECKLINK=no
       ;;
     *-*linux*)
       if test "x$HAVE_PTHREAD_H" = "xyes"; then
         AC_CHECK_LIB(dl, dlopen,
           [
            HAVE_DECKLINK=yes
            DECKLINK_CXXFLAGS=
            DECKLINK_LIBS="-lpthread -ldl"
           ])
       fi
       ;;
     *-*darwin*)
       if test "x$HAVE_PTHREAD_H" = "xyes"; then
         AC_CHECK_LIB(dl, dlopen,
           [
            HAVE_DECKLINK=yes
            DECKLINK_CXXFLAGS=
            DECKLINK_LIBS="-lpthread -ldl"
            HAVE_DECKLINK_OSX=yes
           ])
       fi
       ;;
     *-mingw*)
       HAVE_DECKLINK=yes
       DECKLINK_CXXFLAGS=
       DECKLINK_LIBS="-lole32 -loleaut32"
       HAVE_DECKLINK_WIN=yes
       ;;
     *)
       HAVE_DECKLINK=no
       ;;
   esac

   AC_SUBST(DECKLINK_CXXFLAGS)
   AC_SUBST(DECKLINK_LIBS)
])
AM_CONDITIONAL(DECKLINK_OSX, test "x$HAVE_DECKLINK_OSX" = xyes)
AM_CONDITIONAL(DECKLINK_WIN, test "x$HAVE_DECKLINK_WIN" = xyes)

dnl **** DirectFB ****
translit(dnm, m, l) AM_CONDITIONAL(USE_DIRECTFB, true)
AG_GST_CHECK_FEATURE(DIRECTFB, [directfb], dfbvideosink , [
  PKG_CHECK_MODULES(DIRECTFB, directfb >= 0.9.24, HAVE_DIRECTFB="yes", [
    HAVE_DIRECTFB="no"
  ])
])

dnl **** Wayland ****
translit(dnm, m, l) AM_CONDITIONAL(USE_WAYLAND, true)
AC_PATH_PROG([wayland_scanner], [wayland-scanner])
AG_GST_CHECK_FEATURE(WAYLAND, [wayland sink], wayland , [
  PKG_CHECK_MODULES(WAYLAND, wayland-client >= 1.4.0 libdrm >= 2.4.55 wayland-protocols >= 1.4, [
    if test "x$wayland_scanner" != "x"; then
      HAVE_WAYLAND="yes"
      AC_SUBST(WAYLAND_PROTOCOLS_DATADIR, `$PKG_CONFIG --variable=pkgdatadir wayland-protocols`)
    else
      AC_MSG_RESULT([wayland-scanner is required to build the wayland plugin])
      HAVE_WAYLAND="no"
    fi
    ],
    [ HAVE_WAYLAND="no"
  ])
])

dnl **** WebP ****
translit(dnm, m, l) AM_CONDITIONAL(USE_WEBP, true)
AG_GST_CHECK_FEATURE(WEBP, [WebP], webp , [
  PKG_CHECK_MODULES(WEBP, libwebp >= 0.2.1, [
    HAVE_WEBP="yes" ], [ HAVE_WEBP="no"
  ])
  AC_SUBST(WEBP_CFLAGS)
  AC_SUBST(WEBP_LIBS)
])

dnl *** Daala ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DAALA, true)
AG_GST_CHECK_FEATURE(DAALA, [daala], daala, [
  if test "x$BUILD_EXPERIMENTAL" = "xyes"; then
    PKG_CHECK_MODULES(DAALA, daalaenc daaladec, [
            HAVE_DAALA="yes"
    ], [
            HAVE_DAALA="no"
    ])
    AC_SUBST(DAALA_CFLAGS)
    AC_SUBST(DAALA_LIBS)
  fi
])

dnl *** DTS ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DTS, true)
AG_GST_CHECK_FEATURE(DTS, [dts library], dtsdec, [
  HAVE_DTS="yes"
  AG_GST_CHECK_LIBHEADER(DTS, dca, dca_init, $LIBM, dca.h,
    DTS_LIBS="-ldca $LIBM", HAVE_DTS="no")
  if test $HAVE_DTS = "no"; then
    AG_GST_CHECK_LIBHEADER(DTS, dts_pic, dts_init, $LIBM, dts.h,
      [ DTS_LIBS="-ldts_pic $LIBM"
        AC_DEFINE(DTS_OLD, 1, [Define if an old libdts is used])
      ])
  fi
  AC_SUBST(DTS_LIBS)
])

dnl *** resindvd ***
translit(dnm, m, l) AM_CONDITIONAL(USE_RESINDVD, true)
AG_GST_CHECK_FEATURE(RESINDVD, [resindvd plugin], resindvd, [
  PKG_CHECK_MODULES(DVDNAV, dvdnav >= 4.1.2 dvdread >= 4.1.2, HAVE_RESINDVD="yes", [
    HAVE_RESINDVD="no"
  ])
])

dnl **** Free AAC Encoder (FAAC) ****
translit(dnm, m, l) AM_CONDITIONAL(USE_FAAC, true)
AG_GST_CHECK_FEATURE(FAAC, [AAC encoder plug-in], faac, [
  AG_GST_CHECK_LIBHEADER(FAAC, faac, faacEncOpen, $LIBM, faac.h,
    FAAC_LIBS="-lfaac $LIBM")
  AC_SUBST(FAAC_LIBS)
])

dnl **** Free AAC Decoder (FAAD2) ****
translit(dnm, m, l) AM_CONDITIONAL(USE_FAAD, true)
AG_GST_CHECK_FEATURE(FAAD, [FAAD2 AAC decoder plug-in], faad, [
  AG_GST_CHECK_LIBHEADER(FAAD, faad, NeAACDecOpen, $LIBM, neaacdec.h, [
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <faad.h>]], [[
      #if !defined(FAAD2_VERSION) || !defined(FAAD_FMT_DOUBLE)
      #error Not faad2
      #elif !defined(LATM)
      #error Not faad2 >= 2.7
      #endif
    ]])],[
      HAVE_FAAD="yes"
      FAAD_LIBS="-lfaad"
    ], [
      AC_MSG_WARN([Found libfaad, but version not recent enough. Need faad2 >= 2.7])
      HAVE_FAAD="no"
    ])
  ], [
      HAVE_FAAD="no"
  ])
  AC_SUBST(FAAD_LIBS)
])

dnl *** fbdev ***
translit(dnm, m, l) AM_CONDITIONAL(USE_FBDEV, true)
AG_GST_CHECK_FEATURE(FBDEV, [linux framebuffer], fbdevsink, [
  if test "x$HAVE_FBDEV" != "xyes"; then
    AC_CHECK_HEADER([linux/fb.h], HAVE_FBDEV=yes, HAVE_FBDEV=no)
  fi
])

dnl *** fdkaac ***
translit(dnm, m, l) AM_CONDITIONAL(USE_FDK_AAC, true)
AG_GST_CHECK_FEATURE(FDK_AAC, [fdkaac plugin], fdkaac, [
  PKG_CHECK_MODULES(FDK_AAC, fdk-aac >= 0.1.4, [
    HAVE_FDK_AAC="yes",
    HAVE_FDK_AAC_0_1_4="yes"
    AC_DEFINE(HAVE_FDK_AAC_0_1_4, 1, [Define if we have fdk-aac >= 0.1.4])
  ],[
    PKG_CHECK_MODULES(FDK_AAC, fdk-aac, HAVE_FDK_AAC="yes", HAVE_FDK_AAC="no")
  ])
])

dnl *** flite ***
translit(dnm, m, l) AM_CONDITIONAL(USE_FLITE, true)
AG_GST_CHECK_FEATURE(FLITE, [Flite plugin], flite, [
  AG_GST_CHECK_LIBHEADER(FLITE, flite, flite_init, $LIBM, flite/flite.h,
    FLITE_LIBS="-lflite -lflite_cmu_us_kal -lflite_usenglish -lflite_cmulex $LIBM")

  AC_SUBST(FLITE_CFLAGS)
  AC_SUBST(FLITE_LIBS)
])

dnl *** gsm ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GSM, true)
AG_GST_CHECK_FEATURE(GSM, [GSM library], gsmenc gsmdec, [
  AG_GST_CHECK_LIBHEADER(GSM, gsm, gsm_create, , gsm.h, GSM_LIBS="-lgsm")
  if test $HAVE_GSM != "yes"; then
    AG_GST_CHECK_LIBHEADER(GSM, gsm, gsm_create, , gsm/gsm.h, GSM_LIBS="-lgsm")
    if test $HAVE_GSM = "yes"; then
      AC_DEFINE(GSM_HEADER_IN_SUBDIR, 1, [Define if GSM header in gsm/ subdir])
    fi
  fi
  AC_SUBST(GSM_LIBS)
])

dnl *** fluidSynth ***
translit(dnm, m, l) AM_CONDITIONAL(USE_FLUIDSYNTH, true)
AG_GST_CHECK_FEATURE(FLUIDSYNTH, [fluidsynth], fluidsynth, [
  PKG_CHECK_MODULES(FLUIDSYNTH, fluidsynth >= 1.0, HAVE_FLUIDSYNTH="yes", [
    HAVE_FLUIDSYNTH="no"
  ])
])

dnl *** kate ***
translit(dnm, m, l) AM_CONDITIONAL(USE_KATE, true)
AG_GST_CHECK_FEATURE(KATE, [Kate], kate, [
  PKG_CHECK_MODULES(KATE, kate >= 0.1.7, HAVE_KATE="yes", [
    HAVE_KATE="no"
  ])
  AC_SUBST(KATE_CFLAGS)
  AC_SUBST(KATE_LIBS)
  PKG_CHECK_MODULES(TIGER, tiger >= 0.3.2,
    [
      HAVE_TIGER="yes"
      AC_DEFINE(HAVE_TIGER, 1, [Define if libtiger is available])
    ],
    [
      HAVE_TIGER="no"
    ]
  )
  AM_CONDITIONAL(USE_TIGER, test "x$HAVE_TIGER" = "xyes")
  AC_SUBST(TIGER_CFLAGS)
  AC_SUBST(TIGER_LIBS)
],,,[AM_CONDITIONAL(USE_TIGER, false)])

dnl *** kms ***
translit(dnm, m, l) AM_CONDITIONAL(USE_KMS, true)
AG_GST_CHECK_FEATURE(KMS, [drm/kms libraries], kms, [
  AG_GST_PKG_CHECK_MODULES(GST_ALLOCATORS, gstreamer-allocators-1.0)
  PKG_CHECK_MODULES([KMS_DRM], [libdrm >= 2.4.55], HAVE_KMS=yes, HAVE_KMS=no)
])

dnl *** ladspa ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LADSPA, true)
AG_GST_CHECK_FEATURE(LADSPA, [ladspa], ladspa, [
  AC_CHECK_HEADER(ladspa.h, HAVE_LADSPA="yes", HAVE_LADSPA="no")

  PKG_CHECK_MODULES(LRDF, lrdf, HAVE_LRDF=yes, HAVE_LRDF=no)
  if test $HAVE_LRDF = "yes"; then
    AC_DEFINE(HAVE_LRDF, 1, [Define if we have liblrdf])
  fi
  AC_SUBST(LRDF_LIBS)
  AC_SUBST(LRDF_CFLAGS)
])

dnl *** LCM2  ***
#translit(dnm, m, l) AM_CONDITIONAL(USE_LCMS2, true)
AG_GST_CHECK_FEATURE(LCMS2, [LCMS colormanagement plugin], lcms2, [
  PKG_CHECK_MODULES(LCMS2, lcms2 >= 2.7, HAVE_LCMS2="yes", [
    HAVE_LCMS2="no"
  ])
   AC_SUBST(LCMS2_LIBS)
])

dnl *** LV2 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LV2, true)
AG_GST_CHECK_FEATURE(LV2, [lv2], lv2, [
  PKG_CHECK_MODULES(LILV, lilv-0 >= 0.22, [
    HAVE_LV2="yes",
    HAVE_LILV_0_22="yes"
    AC_DEFINE(HAVE_LILV_0_22, 1, [Define if we have liblilv >= 0.22])
  ],[
    PKG_CHECK_MODULES(LILV, lilv-0 >= 0.16, HAVE_LV2="yes", HAVE_LV2="no")
  ])
  AC_SUBST(LILV_CFLAGS)
  AC_SUBST(LILV_LIBS)
])

dnl *** libde265 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBDE265, true)
AG_GST_CHECK_FEATURE(LIBDE265, [libde265 HEVC/H.265 decoder], libde265, [
  PKG_CHECK_MODULES(LIBDE265, libde265 >= 0.9, HAVE_LIBDE265="yes", HAVE_LIBDE265="no")
  AC_SUBST(LIBDE265_CFLAGS)
  AC_SUBST(LIBDE265_LIBS)
])

dnl *** libmms ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBMMS, true)
AG_GST_CHECK_FEATURE(LIBMMS, [mms protocol library], libmms, [
  dnl check with pkg-config first
  PKG_CHECK_MODULES(LIBMMS, libmms >= 0.4, HAVE_LIBMMS="yes", [
    HAVE_LIBMMS="no"
  ])
])
AC_SUBST(LIBMMS_LIBS)

dnl *** libsrt ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SRT, true)
AG_GST_CHECK_FEATURE(SRT, [srt library], srt, [
  PKG_CHECK_MODULES(SRT, srt, HAVE_SRT="yes", HAVE_SRT=no)
  AC_SUBST(SRT_LIBS)
  AC_SUBST(SRT_CFLAGS)
])

dnl *** libsrtp ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SRTP, true)
AG_GST_CHECK_FEATURE(SRTP, [srtp library], srtp, [
  HAVE_SRTP="no"
  AG_GST_PKG_CHECK_MODULES(SRTP, libsrtp2 >= 2.1.0)
  if test x"$HAVE_SRTP" = x"yes"; then
    AC_DEFINE([HAVE_SRTP2], 1, [Define if libsrtp2 is used])
  else
    PKG_CHECK_MODULES(SRTP, libsrtp, HAVE_SRTP="yes",
      AG_GST_CHECK_LIBHEADER(SRTP, srtp, srtp_init, , srtp/srtp.h, SRTP_LIBS="-lsrtp")
    )
  fi
  AC_SUBST(SRTP_LIBS)
  AC_SUBST(SRTP_CFLAGS)
])

dnl *** dtls ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DTLS, true)
AG_GST_CHECK_FEATURE(DTLS, [DTLS plugin], dtls, [
  PKG_CHECK_MODULES(DTLS, [ openssl >= 1.0.1  libcrypto ], [
    HAVE_DTLS="yes"
    AC_SUBST(DTLS_CFLAGS)
    AC_SUBST(DTLS_LIBS)
  ], [
    HAVE_DTLS="no"
  ])
])

dnl *** ttml ***
translit(dnm, m, l) AM_CONDITIONAL(USE_TTML, true)
AG_GST_CHECK_FEATURE(TTML, [TTML plugin], ttml, [
  PKG_CHECK_MODULES(TTML, [ libxml-2.0 >= 2.9.2 pango cairo pangocairo ], [
    HAVE_TTML="yes"
  ], [
    HAVE_TTML="no"
  ])
])

dnl *** modplug ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MODPLUG, true)
AG_GST_CHECK_FEATURE(MODPLUG, modplug, modplug, [
  if test "x$HAVE_CXX" != "xyes"; then
    AC_MSG_WARN([Not compiling modplug plugin as it requires a C++ compiler])
    HAVE_MODPLUG="no"
  else
    PKG_CHECK_MODULES(MODPLUG, libmodplug, HAVE_MODPLUG="yes", HAVE_MODPLUG="no")
  fi
  AC_SUBST(MODPLUG_CFLAGS)
  AC_SUBST(MODPLUG_LIBS)
])

dnl *** mjpegtools version info ***
dnl some may prefer older version (given quirks above)
dnl hm, no version info seems available within mjpegtools headers
dnl and API really moves along
echo
PKG_CHECK_EXISTS(mjpegtools >= 1.6.1.93 mjpegtools < 1.8.0, [
  mjpegtools_api=10601
], [
  PKG_CHECK_EXISTS(mjpegtools >= 1.8.0 mjpegtools < 1.9.0, [
    mjpegtools_api=10800
  ], [
    PKG_CHECK_EXISTS(mjpegtools >= 2.0.0, [
      mjpegtools_api=20000
    ], [
      PKG_CHECK_MODULES(MJPEG, mjpegtools >= 1.9.0 mjpegtools < 2.0.0, [
        dnl logging API changed in release candidates
        OLD_CFLAGS="$CFLAGS"
        OLD_LIBS="$LIBS"
        CFLAGS="$MJPEG_CFLAGS"
        LIBS="$LIBS $MJPEG_LIBS -lmjpegutils $LIBM $PTHREAD_LIBS"
        AC_CHECK_FUNC(mjpeg_loglev_t, [
          mjpegtools_api=10903
          ], [
          mjpegtools_api=10900
          ])
        CFLAGS="$OLD_CFLAGS"
        LIBS="$OLD_LIBS"
      ], [
        mjpegtools_api=0
      ])
    ])
  ])
])
AC_DEFINE_UNQUOTED(GST_MJPEGTOOLS_API, $mjpegtools_api,
  [mjpegtools API evolution])

dnl *** mpeg2enc ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MPEG2ENC, true)
AG_GST_CHECK_FEATURE(MPEG2ENC, [mpeg2enc], mpeg2enc, [
  HAVE_MPEG2ENC="no"
  dnl we require a c++ compiler for this one
  if [ test x$HAVE_CXX = xyes ]; then
    dnl libmpeg2enc was first included in mjpegtools-1.6.2-rc3 (1.6.1.92)
    dnl since many distros include mjpegtools specifically without mplex
    dnl and mpeg2enc, we check for mpeg2enc on its own, too.
    PKG_CHECK_MODULES(MPEG2ENC, mjpegtools >= 1.6.1.93, [
      dnl HACK because mpeg2enc 1.8.0 header files have a spurious 'include config.h'
      touch config.h
      dnl switch over to c++ to test things
      AC_LANG([C++])
      OLD_CPPFLAGS="$CPPFLAGS"
      dnl HACK as above
      CPPFLAGS_GOOD="$CPPFLAGS $MPEG2ENC_CFLAGS"
      CPPFLAGS="$CPPFLAGS_GOOD -I."
      dnl check headers
      mpeg2enc_headers_ok=no
      AC_CHECK_HEADER([mpeg2encoder.hh], [
        MPEG2ENC_LIBS="$MPEG2ENC_LIBS -lmpeg2encpp $LIBM $PTHREAD_LIBS"
        OLD_LIBS="$LIBS"
        LIBS="$LIBS $MPEG2ENC_LIBS"
        AC_MSG_CHECKING([for valid mpeg2enc objects])
        AC_RUN_IFELSE([AC_LANG_SOURCE([[
          #include <mpeg2encoder.hh>
          #include <mpeg2encoptions.hh>

          int
          main (int   argc,
                char *argv[])
          {
            MPEG2EncOptions *options = new MPEG2EncOptions ();
            MPEG2Encoder *encoder = new MPEG2Encoder (*options);
            return 0;
          }
        ]])],[
          AC_MSG_RESULT(yes)
          dnl so far so good, let's check more things:
          dnl mjpegtools-1.8.0 does not install the required
          dnl mpeg2syntaxcodes.h header by default, and a new release
          dnl is not in sight, so check for this oversight in case
          dnl distros or folks have fixed this themselves
          if test "$mjpegtools_api" -ge "10800"; then
            AC_CHECK_HEADER([mpeg2syntaxcodes.h], [
                mpeg2enc_headers_ok=yes
              ], [
                mpeg2enc_headers_ok=no
              ])
          else
            mpeg2enc_headers_ok=yes
          fi
          if test "x$mpeg2enc_headers_ok" = "xyes"; then
            HAVE_MPEG2ENC="yes"
          fi
          CPPFLAGS="$CPPFLAGS_GOOD"
          AC_SUBST(MPEG2ENC_CFLAGS)
          AC_SUBST(MPEG2ENC_LIBS)
        ],[
          AC_MSG_RESULT(no)
        ],[])
        LIBS="$OLD_LIBS"
      ])

      CPPFLAGS="$OLD_CPPFLAGS"
      AC_LANG([C])
    ], [
      HAVE_MPEG2ENC="no"
    ])
  fi
])

dnl *** mplex ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MPLEX, true)
AG_GST_CHECK_FEATURE(MPLEX, [mplex], mplex, [
  HAVE_MPLEX="no"
  dnl we require a c++ compiler for this one
  if [ test x$HAVE_CXX = xyes ]; then
    dnl libmplex was first included in mjpegtools-1.6.2-rc4 (1.6.1.93)
    dnl since many distros include mjpegtools specifically without mplex
    dnl and mpeg2enc, we check for mplex on its own, too.
    dnl libmplex < 1.9rc? has fuzzy ABI, valgrind and other problems
    PKG_CHECK_MODULES(MPLEX, mjpegtools >= 1.9.0, [
      dnl HACK because mplex 2.0.0 header files have a spurious 'include config.h'
      touch config.h
      dnl switch over to c++ to test things
      AC_LANG([C++])
      OLD_CPPFLAGS="$CPPFLAGS"
      dnl HACK as above
      CPPFLAGS_GOOD="$CPPFLAGS $MPLEX_CFLAGS"
      CPPFLAGS="$CPPFLAGS_GOOD -I."
      AC_CHECK_HEADER(interact.hpp, [
        MPLEX_LIBS="$MPLEX_LIBS -lmplex2 $LIBM"
        OLD_LIBS="$LIBS"
        LIBS="$LIBS $MPLEX_LIBS"
        OLD_LDFLAGS="$LDFLAGS"
        LDFLAGS="$LDFLAGS $MPLEX_LDFLAGS"
        dnl older libmplex uses off_t SegmentSize (), which leads to fuzzy ABI;
        dnl don't want this here
        AC_MSG_CHECKING([for valid mplex objects])
        AC_RUN_IFELSE([AC_LANG_SOURCE([[

#include <interact.hpp>
#include <outputstrm.hpp>
#include <multiplexor.hpp>
int
main (int   argc,
      char *argv[])
{
  class TestOutputStream : public OutputStream {
  public:
    TestOutputStream () : OutputStream () { }
    void Write (uint8_t *a, unsigned int b) { }
    void NextSegment () { }
    uint64_t SegmentSize () { }
    void Close () { }
    int Open () { }
};
  MultiplexJob *job = new MultiplexJob ();
  vector<IBitStream *> inputs;
  job->SetupInputStreams (inputs);
  TestOutputStream *out = new TestOutputStream ();
  Multiplexor *mux = new Multiplexor(*job, *out, NULL);
  return 0;
}
        ]])],[
          HAVE_MPLEX="yes"
          CPPFLAGS="$CPPFLAGS_GOOD"
          AC_SUBST(MPLEX_CFLAGS)
          AC_SUBST(MPLEX_LIBS)
          AC_SUBST(MPLEX_LDFLAGS)
          AC_MSG_RESULT(yes)
        ],[AC_MSG_RESULT(no)],[])
        LIBS="$OLD_LIBS"
        LDFLAGS="$OLD_LDFLAGS"
      ])
      CPPFLAGS="$OLD_CPPFLAGS"
      AC_LANG([C])
    ], HAVE_MPLEX="no")
  fi
])

dnl *** musepack ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MUSEPACK, true)
AG_GST_CHECK_FEATURE(MUSEPACK, [musepackdec], musepack, [
  AC_CHECK_HEADER([mpc/mpcdec.h], [
    HAVE_MUSEPACK="yes"
    MUSEPACK_LIBS="-lmpcdec"
    AC_SUBST(MUSEPACK_LIBS)
    ], [AC_CHECK_HEADER([mpcdec/mpcdec.h], [
      HAVE_MUSEPACK="yes"
      MUSEPACK_LIBS="-lmpcdec"
      AC_DEFINE(MPC_IS_OLD_API, 1, [Define if the old MusePack API is used])
      AC_SUBST(MUSEPACK_LIBS)
    ], [HAVE_MUSEPACK="no"])])
])

dnl *** neon ***
translit(dnm, m, l) AM_CONDITIONAL(USE_NEON, true)
AG_GST_CHECK_FEATURE(NEON, [neon http client plugins], neonhttpsrc, [
  PKG_CHECK_MODULES(NEON, neon >= 0.27.0  neon <= 0.30.99, HAVE_NEON="yes", [
    HAVE_NEON="no"
  ])
  AC_SUBST(NEON_CFLAGS)
  AC_SUBST(NEON_LIBS)
])

dnl *** ofa ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OFA, true)
AG_GST_CHECK_FEATURE(OFA, [ofa plugins], ofa, [
  PKG_CHECK_MODULES(OFA, libofa >= 0.9.3, HAVE_OFA="yes", [
    HAVE_OFA="no"
  ])
  AC_SUBST(OFA_CFLAGS)
  AC_SUBST(OFA_LIBS)
])

dnl *** OpenAL ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OPENAL, true)
AG_GST_CHECK_FEATURE(OPENAL, [OpenAL plugin], openal, [
  PKG_CHECK_MODULES(OPENAL, openal >= 1.14, HAVE_OPENAL="yes", [
    HAVE_OPENAL="no"
  ])
  AC_SUBST(OPENAL_CFLAGS)
  AC_SUBST(OPENAL_LIBS)
])

dnl *** opencv ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OPENCV, true)
AG_GST_CHECK_FEATURE(OPENCV, [opencv plugins], opencv, [
  dnl we specify a max. version too because we set CV_NO_BACKWARD_COMPATIBILITY
  dnl and don't want the build to break when a new opencv version comes out.
  dnl Need to adjust this upwards once we know that our code compiles fine with
  dnl a new version and the no-backward-compatibility define. (There doesn't
  dnl seem to be a switch to suppress the warnings the cvcompat.h header
  dnl causes.)
  PKG_CHECK_MODULES(OPENCV, opencv >= 2.3.0 opencv < 3.5.0 , [
    AC_PROG_CXX
    AC_LANG([C++])
    OLD_CPPFLAGS=$CPPFLAGS
    CPPFLAGS=$OPENCV_CFLAGS
    something_not_found=no
    AC_CHECK_HEADERS([opencv2/core/core_c.h \
                      opencv2/core/version.hpp \
                      opencv2/highgui/highgui_c.h \
                      opencv2/imgproc/imgproc.hpp \
                      opencv2/imgproc/imgproc_c.h \
                      opencv2/objdetect/objdetect.hpp \
                      opencv2/video/background_segm.hpp], [], [something_not_found=yes])

    dnl Only required for OpenCV >= 3, we have compat code for older versions
    PKG_CHECK_MODULES(OPENCV_3, opencv >= 3, [
      dnl check opencv_contrib headers (not always present in opencv distributions)
      AC_CHECK_HEADERS([opencv2/bgsegm.hpp], [], [something_not_found=yes])],
      [NOT_A_PROBLEM=yes])

    CPPFLAGS=$OLD_CPPFLAGS
    AC_LANG([C])

    if test $something_not_found = "yes"; then
      AC_MSG_RESULT([not all opencv2 headers were found])
      HAVE_OPENCV="no"
    else
      HAVE_OPENCV="yes"
    fi
  ], [
    HAVE_OPENCV="no"
  ])
  OPENCV_PREFIX="`$PKG_CONFIG --variable=prefix opencv`"
  AC_DEFINE_UNQUOTED(OPENCV_PREFIX, "$OPENCV_PREFIX", [opencv install prefix])
  AC_SUBST(OPENCV_PREFIX)
  AC_SUBST(OPENCV_CFLAGS)
  AC_SUBST(OPENCV_LIBS)
  dnl the OpenCV prefix is used at runtime for some object properties, and also
  dnl used here to detect which directory is the correct one. For the second
  dnl case, make sure the right sysroot is used, ensuring correct behavior
  dnl during cross compilation.
  if test -d "$PKG_CONFIG_SYSROOT_DIR/$OPENCV_PREFIX/share/opencv/"; then
    AC_DEFINE_UNQUOTED(OPENCV_PATH_NAME, ["opencv"], [OpenCV path name])
  else
    AC_DEFINE_UNQUOTED(OPENCV_PATH_NAME, ["OpenCV"], [OpenCV path name])
  fi
])

dnl *** OpenEXR ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OPENEXR, true)
AG_GST_CHECK_FEATURE(OPENEXR, [openexr library], openexr, [
  AG_GST_PKG_CHECK_MODULES(OPENEXR, OpenEXR)
])

dnl *** openh264 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OPENH264, true)
AG_GST_CHECK_FEATURE(OPENH264, [openh264 library], openh264, [
  AG_GST_PKG_CHECK_MODULES(OPENH264, openh264 >= 1.3.0)
])

dnl *** OpenJPEG ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OPENJPEG, true)
AG_GST_CHECK_FEATURE(OPENJPEG, [openjpeg library], openjpeg, [
  HAVE_OPENJPEG="no"
  AG_GST_PKG_CHECK_MODULES(OPENJPEG, libopenjp2 >= 2.1)
  if test x"$HAVE_OPENJPEG" != x"yes"; then
    # Fallback to v1.5
    OPENJPEG_LIBS=""
    PKG_CHECK_MODULES(OPENJPEG, libopenjpeg1,
      HAVE_OPENJPEG="yes",
      AG_GST_CHECK_LIBHEADER(OPENJPEG, openjpeg, opj_version, , openjpeg.h, OPENJPEG_LIBS="-lopenjpeg")
    )
    if test x"$HAVE_OPENJPEG" = x"yes"; then
      AC_DEFINE([HAVE_OPENJPEG_1], 1, [Define if OpenJPEG 1 is used])
    fi
  fi
  AC_SUBST(OPENJPEG_CFLAGS)
  AC_SUBST(OPENJPEG_LIBS)
])

dnl *** OpenMPT ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OPENMPT, true)
AG_GST_CHECK_FEATURE(OPENMPT, openmpt, openmpt, [
  PKG_CHECK_MODULES(OPENMPT, libopenmpt, HAVE_OPENMPT="yes", HAVE_OPENMPT="no")
  AC_SUBST(OPENMPT_CFLAGS)
  AC_SUBST(OPENMPT_LIBS)
])

dnl *** OpenNI2 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OPENNI2, true)
AG_GST_CHECK_FEATURE(OPENNI2, [openni2 library], openni2, [
  PKG_CHECK_MODULES(OPENNI2, libopenni2 >= 0.26, HAVE_OPENNI2="yes", [ HAVE_OPENNI2="no" ] )
  AC_SUBST(OPENNI2_CFLAGS)
  AC_SUBST(OPENNI2_LIBS)
])

dnl *** Opus ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OPUS, true)
AG_GST_CHECK_FEATURE(OPUS, [opus], opus, [
  PKG_CHECK_MODULES(OPUS, opus >= 0.9.4, [
          AC_DEFINE([HAVE_OPUS], 1, [Define if Opus >= 0.9.4 is installed])
          HAVE_OPUS="yes"
  ], [
          HAVE_OPUS="no"
  ])
  AC_SUBST(OPUS_CFLAGS)
  AC_SUBST(OPUS_LIBS)
])

dnl *** rsvg ***
translit(dnm, m, l) AM_CONDITIONAL(USE_RSVG, true)
AG_GST_CHECK_FEATURE(RSVG, [rsvg decoder], rsvg, [
  AG_GST_PKG_CHECK_MODULES(RSVG, librsvg-2.0 >= 2.36.2)
])

dnl *** gl ***
HAVE_GL="no"
translit(dnm, m, l) AM_CONDITIONAL(USE_GL, true)
AG_GST_CHECK_FEATURE(GL, [gl elements], gl, [
  if test "x$HAVE_GST_GL" = "xyes"; then
    HAVE_GL="yes"
  fi
])
AM_CONDITIONAL(USE_GL, test "x$HAVE_GL" = "xyes")
AM_CONDITIONAL(USE_OPENGL, test "x$GST_GL_HAVE_API_GL" = "x1")

dnl *** Vulkan ***
translit(dnm, m, l) AM_CONDITIONAL(USE_VULKAN, true)
AG_GST_CHECK_FEATURE(VULKAN, [Vulkan elements], vulkan, [
  AC_CHECK_HEADER(vulkan/vulkan.h, [
    AC_CHECK_LIB(vulkan, vkCreateDevice, [
      VULKAN_LIBS="-lvulkan"
      AC_SUBST(VULKAN_LIBS)
      if test "x$GST_VULKAN_HAVE_WINDOW_XCB" = "x1"; then
        HAVE_VULKAN="yes"
      fi
      if test "x$GST_VULKAN_HAVE_WINDOW_WAYLAND" = "x1"; then
        HAVE_VULKAN="yes"
      fi
    ], [])
  ], [])
])

dnl *** teletextdec ***
translit(dnm, m, l) AM_CONDITIONAL(USE_TELETEXTDEC, true)
AG_GST_CHECK_FEATURE(TELETEXTDEC, [Teletext decoder], teletextdec, [
  AG_GST_PKG_CHECK_MODULES(TELETEXTDEC, zvbi-0.2)
 ])

dnl *** wildmidi ***
translit(dnm, m, l) AM_CONDITIONAL(USE_WILDMIDI, true)
AG_GST_CHECK_FEATURE(WILDMIDI, [wildmidi midi soft synth plugin], wildmidi, [
  AG_GST_CHECK_LIBHEADER(WILDMIDI, WildMidi,
                         WildMidi_Init, ,
                         wildmidi_lib.h,
                         WILDMIDI_LIBS="-lWildMidi")
  WILDMIDI_CFLAGS=
  WILDMIDI_LIBS=
  if test "x$HAVE_WILDMIDI" = "xyes"; then
    AC_MSG_CHECKING([for wildmidi >= 4.0])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <wildmidi_lib.h>]], [[
        #if LIBWILDMIDI_VER_MAJOR == 0 && LIBWILDMIDI_VER_MINOR < 4
        #error "wildmidi too old"
        #endif
    ]])], [ AC_MSG_RESULT(yes)
      HAVE_WILDMIDI="yes"
      WILDMIDI_CFLAGS=
      WILDMIDI_LIBS=-lWildMidi
    ], [ AC_MSG_RESULT(no)
      HAVE_WILDMIDI="no"
    ])
  else
    AC_MSG_WARN([WildMidi library or development headers not found])
  fi
  AC_SUBST(WILDMIDI_CFLAGS)
  AC_SUBST(WILDMIDI_LIBS)
])

dnl **** Smooth Streaming ****
translit(dnm, m, l) AM_CONDITIONAL(USE_SMOOTHSTREAMING, true)
AG_GST_CHECK_FEATURE(SMOOTHSTREAMING, [Smooth Streaming plug-in], smoothstreaming, [
  PKG_CHECK_MODULES(LIBXML2, libxml-2.0 >= 2.4,
    [HAVE_LIBXML2="yes"
     HAVE_SMOOTHSTREAMING="yes"],
    [HAVE_LIBXML2="no"
     HAVE_SMOOTHSTREAMING="no"])
  AC_SUBST(LIBXML2_CFLAGS)
  AC_SUBST(LIBXML2_LIBS)
])

dnl *** sndfile ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SNDFILE, true)
AG_GST_CHECK_FEATURE(SNDFILE, [sndfile plug-in], sfdec sfenc, [
  PKG_CHECK_MODULES(SNDFILE, sndfile >= 1.0.16, HAVE_SNDFILE="yes", HAVE_SNDFILE="no")
  AC_SUBST(SNDFILE_CFLAGS)
  AC_SUBST(SNDFILE_LIBS)
])

dnl *** soundtouch ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SOUNDTOUCH, true)
AG_GST_CHECK_FEATURE(SOUNDTOUCH, [soundtouch plug-in], soundtouch, [
  dnl We only need -lBPM on soundtouch < 1.4
  dnl We check for libSoundTouch since Debian used it before upstream
  dnl added a pkgconfig file.
  HAVE_SOUNDTOUCH_1_4=yes
  PKG_CHECK_MODULES(SOUNDTOUCH, soundtouch,
    [HAVE_SOUNDTOUCH=yes],
    [PKG_CHECK_MODULES(SOUNDTOUCH, soundtouch-1.4,
      [HAVE_SOUNDTOUCH=yes],
      [PKG_CHECK_MODULES(SOUNDTOUCH, soundtouch-1.0,
        [HAVE_SOUNDTOUCH=yes
          HAVE_SOUNDTOUCH_1_4=no
          SOUNDTOUCH_LIBS="$SOUNDTOUCH_LIBS -lBPM"],
        [PKG_CHECK_MODULES(SOUNDTOUCH, libSoundTouch >= 1.4,
          [HAVE_SOUNDTOUCH=yes],
          [PKG_CHECK_MODULES(SOUNDTOUCH, libSoundTouch,
            [HAVE_SOUNDTOUCH=yes
              HAVE_SOUNDTOUCH_1_4=no
              SOUNDTOUCH_LIBS="$SOUNDTOUCH_LIBS -lBPM"],
            HAVE_SOUNDTOUCH=no)])])])])
  AC_SUBST(SOUNDTOUCH_CFLAGS)
  AC_SUBST(SOUNDTOUCH_LIBS)
  if test "x$HAVE_CXX" != "xyes"; then
    USE_SOUNDTOUCH=false
    AC_MSG_NOTICE([Not building soundtouch plugin: no C++ compiler found])
  fi
  if test "x$HAVE_SOUNDTOUCH" = "xyes" -a "x$HAVE_SOUNDTOUCH_1_4" = "xyes"; then
     AC_DEFINE([HAVE_SOUNDTOUCH_1_4], [1],
         [Defined if the available libSoundTouch is >= 1.4])
  fi
])

dnl *** spc ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SPC, true)
AG_GST_CHECK_FEATURE(SPC, [spc decoder], spc, [
  AC_CHECK_HEADER(openspc.h, [
    AC_CHECK_LIB(openspc, OSPC_Init, [
      SPC_LIBS="-lopenspc"
      AC_SUBST(SPC_LIBS)
      HAVE_SPC=yes
    ], [
      HAVE_SPC=no
    ])
  ], [
    HAVE_SPC=no
  ])
])

dnl *** gme ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GME, true)
AG_GST_CHECK_FEATURE(GME, [gme decoder], gme, [
  AC_CHECK_HEADER(gme/gme.h, [
    AC_CHECK_LIB(gme, gme_new_emu, [
      GME_LIBS="-lgme"
      AC_SUBST(GME_LIBS)
      HAVE_GME=yes
    ], [
      HAVE_GME=no
    ])
  ], [
    HAVE_GME=no
  ])

  if test "x$HAVE_GME" = "xyes"; then
    AC_CHECK_LIB(gme, gme_enable_accuracy, [
      AC_DEFINE(HAVE_LIBGME_ACCURACY, 1, [Define if gme 0.5.6 or newer is available])
      ])
  fi
])

dnl *** dvb ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DVB, true)
AG_GST_CHECK_FEATURE(DVB, [DVB Source], dvb, [
  AC_MSG_CHECKING([Checking for up to date dvb installation])
  AC_CHECK_HEADER(linux/dvb/version.h, [
    AC_MSG_CHECKING([for up-to-date dvb API])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <linux/dvb/version.h>]], [[
        #if DVB_API_VERSION < 5 || (DVB_API_VERSION == 5 && DVB_API_VERSION_MINOR <= 4)
        #error "Incompatible dvb API"
        #endif
    ]])], [ AC_MSG_RESULT(yes)
      HAVE_DVB="yes"
    ], [ AC_MSG_RESULT(no)
      HAVE_DVB="no"
    ])
  ], [HAVE_DVB="no"])
])

dnl *** acm ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ACM, true)
AG_GST_CHECK_FEATURE(ACM, [Windows ACM library], acm, [
  AC_MSG_CHECKING([Checking for ACM support])
  AC_CHECK_HEADERS([windows.h msacm.h],
      [HAVE_ACM="yes"
       AC_SUBST(ACMENC_CFLAGS)
       AC_SUBST(ACMMP3DEC_CFLAGS)
      ], [HAVE_ACM="no"],
      [AC_INCLUDES_DEFAULT
#include <windows.h>])
])

dnl *** vdpau ***
translit(dnm, m, l) AM_CONDITIONAL(USE_VDPAU, true)
AG_GST_CHECK_FEATURE(VDPAU, [VDPAU], vdpau, [
  VDPAU_CFLAGS=
  VDPAU_LIBS=-lvdpau
  HAVE_VDPAU=no

  PKG_CHECK_MODULES(VDPAU, vdpau, [HAVE_VDPAU="yes"],
  [
    if test "$HAVE_VDPAU" = "no"; then
      saved_CPPFLAGS="$CPPFLAGS"
      AC_CHECK_HEADER([vdpau/vdpau.h], [HAVE_VDPAU_H=yes])

      if test -z "$HAVE_VDPAU_H"; then
        dnl Didn't find VDPAU header straight away.
        dnl Try /usr/include/nvidia. Need to clear caching vars first
        AC_MSG_NOTICE([VDPAU header not in standard path. Checking /usr/include/nvidia])
        unset ac_cv_header_vdpau_vdpau_h
        unset ac_cv_header_vdpau_vdpau_x11_h
        VDPAU_CFLAGS="-I/usr/include/nvidia"
        VDPAU_LIBS="-L/usr/lib/nvidia -lvdpau"
        CPPFLAGS="$VDPAU_CFLAGS $saved_CPPFLAGS"
        AC_CHECK_HEADER([vdpau/vdpau.h], [HAVE_VDPAU_H=yes])
      fi

      AC_CHECK_HEADER([vdpau/vdpau_x11.h], [HAVE_VDPAU_X11_H=yes])

      if test "x$HAVE_VDPAU_H" = "xyes" -a "x$HAVE_VDPAU_X11_H" = "xyes"; then
        dnl Found the headers - look for the lib
        AC_MSG_NOTICE([VDPAU headers found. Checking libraries])
        saved_LIBS="$LIBS"
        LIBS="$VDPAU_LIBS $saved_LIBS"
        AC_CHECK_LIB(vdpau,vdp_device_create_x11,[HAVE_VDPAU="yes"])
        LIBS="$saved_LIBS"
      fi
    fi
  ])

  if test "$HAVE_VDPAU" = "yes"; then
    AC_MSG_NOTICE([Found up to date VDPAU installation])
    AC_SUBST(VDPAU_CFLAGS)
    AC_SUBST(VDPAU_LIBS)
  fi
])

dnl *** sbc ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SBC, true)
AG_GST_CHECK_FEATURE(SBC, [SBC bluetooth audio codec], sbc, [
  AG_GST_PKG_CHECK_MODULES(SBC, [sbc >= 1.0])
])

dnl *** zbar ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ZBAR, true)
AG_GST_CHECK_FEATURE(ZBAR, [ZBar barcode detector], zbar, [
  AG_GST_PKG_CHECK_MODULES(ZBAR, zbar >= 0.9)
])

dnl *** rtmp ***
translit(dnm, m, l) AM_CONDITIONAL(USE_RTMP, true)
AG_GST_CHECK_FEATURE(RTMP, [rtmp library], rtmp, [
  AG_GST_PKG_CHECK_MODULES(RTMP, librtmp)
])

dnl *** spandsp ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SPANDSP, true)
AG_GST_CHECK_FEATURE(SPANDSP, [Spandsp], spandsp, [
  PKG_CHECK_MODULES(SPANDSP, spandsp >= 0.0.6, [
    HAVE_SPANDSP="yes" ], [
    HAVE_SPANDSP="no"
  ])
])
AC_SUBST(SPANDSP_CFLAGS)
AC_SUBST(SPANDSP_LIBS)

dnl *** hls-crypto ***
AC_ARG_WITH([hls-crypto],
  AS_HELP_STRING([--with-hls-crypto=auto|nettle|libgcrypt|openssl], [
    which cryptographic library version to compile against for hls (default: auto)
  ]), [
    AS_CASE([$with_hls_crypto],
      [nettle],[],
      [libgcrypt],[],
      [openssl],[],
      [with_hls_crypto=auto]
    )
  ]
)

dnl *** hls ***
translit(dnm, m, l) AM_CONDITIONAL(USE_HLS, true)
AG_GST_CHECK_FEATURE(HLS, [http live streaming plugin], hls, [
  AS_CASE([$with_hls_crypto],
    [nettle], [
      PKG_CHECK_MODULES(NETTLE, nettle, [
        AC_DEFINE(HAVE_NETTLE, 1, [Define if nettle is available])
        HAVE_HLS="yes"
      ],[AC_MSG_ERROR([Could not find nettle library])])
    ],
    [libgcrypt], [
      AM_PATH_LIBGCRYPT([1.2.0], [
        AC_DEFINE(HAVE_LIBGCRYPT, 1, [Define if libgcrypt is available])
        HAVE_HLS="yes"
      ],[AC_MSG_ERROR([Could not find libgcrypt library])])
    ],
    [openssl], [
      PKG_CHECK_MODULES(OPENSSL, openssl, [
        AC_DEFINE(HAVE_OPENSSL, 1, [Define if openssl is available])
        HAVE_HLS="yes"
      ],[AC_MSG_ERROR([Could not find openssl library])])
    ],
    [
      dnl Try to find a valid crypto library
      PKG_CHECK_MODULES(NETTLE, nettle, [
        AC_DEFINE(HAVE_NETTLE, 1, [Define if nettle is available])
        HAVE_HLS="yes"
      ],[
        AM_PATH_LIBGCRYPT([1.2.0], [
          AC_DEFINE(HAVE_LIBGCRYPT, 1, [Define if libgcrypt is available])
          HAVE_HLS="yes"
        ],[
          PKG_CHECK_MODULES(OPENSSL, openssl, [
            AC_DEFINE(HAVE_OPENSSL, 1, [Define if openssl is available])
            HAVE_HLS="yes"
          ],[
            HAVE_HLS="no"
          ])
        ])
      ])
    ]
  )
])

dnl *** x265 (H.265/HEVC encoder) ***
translit(dnm, m, l) AM_CONDITIONAL(USE_X265, true)
AG_GST_CHECK_FEATURE(X265, [x265 plug-in], x265, [
  AG_GST_PKG_CHECK_MODULES(X265, x265)
])

dnl *** WebRTC Audio Processing ***
dnl Note: We test for a specific version as there is no API stability
translit(dnm, m, l) AM_CONDITIONAL(USE_WEBRTCDSP, true)
AG_GST_CHECK_FEATURE(WEBRTCDSP, [WebRTC Audio Processing], webrtcdsp, [

  AG_GST_PKG_CHECK_MODULES(WEBRTCDSP, webrtc-audio-processing >= 0.2 webrtc-audio-processing < 0.4)

  # On Android, C++ headers comes with the STL implementation
  AS_CASE(["${host}"],
    [*android*],
      [
        PKG_CHECK_MODULES(GNUSTL, gnustl, [
          WEBRTCDSP_CFLAGS="$WEBRTCDSP_CFLAGS $GNUSTL_CFLAGS"
          WEBRTCDSP_LIBS="$WEBRTCDSP_LIBS $GNUSTL_LIBS"])
      ])
  AC_SUBST([WEBRTCDSP_CFLAGS])
  AC_SUBST([WEBRTCDSP_LIBS])

  AC_LANG_PUSH([C++])
  old_CPPFLAGS=$CPPFLAGS
  CPPFLAGS="-std=c++11 $CPPFLAGS $GNUSTL_CFLAGS"
  AC_CHECK_HEADER([vector],[],[HAVE_WEBRTCDSP=no])
  CPPFLAGS=$old_CPPFLAGS
  AC_LANG_POP([C++])
])

dnl *** WebRTC ***
translit(dnm, m, l) AM_CONDITIONAL(USE_WEBRTC, true)
AG_GST_CHECK_FEATURE(WEBRTC, [WebRTC], webrtc, [
  PKG_CHECK_MODULES(NICE, nice >= 0.1.14, [
    HAVE_WEBRTC="yes" ], [
    HAVE_WEBRTC="no"
  ])
])

else

dnl not building plugins with external dependencies,
dnl but we still need to set the conditionals

AM_CONDITIONAL(USE_ASSRENDER, false)
AM_CONDITIONAL(USE_AOM, false)
AM_CONDITIONAL(USE_VOAMRWBENC, false)
AM_CONDITIONAL(USE_VOAACENC, false)
AM_CONDITIONAL(USE_BS2B, false)
AM_CONDITIONAL(USE_BZ2, false)
AM_CONDITIONAL(USE_CHROMAPRINT, false)
AM_CONDITIONAL(USE_CURL, false)
AM_CONDITIONAL(USE_SSH2, false)
AM_CONDITIONAL(USE_DASH, false)
AM_CONDITIONAL(USE_DC1394, false)
AM_CONDITIONAL(USE_DECKLINK, false)
AM_CONDITIONAL(DECKLINK_OSX, false)
AM_CONDITIONAL(DECKLINK_WIN, false)
AM_CONDITIONAL(USE_DIRECTFB, false)
AM_CONDITIONAL(USE_WAYLAND, false)
AM_CONDITIONAL(USE_DAALA, false)
AM_CONDITIONAL(USE_DTS, false)
AM_CONDITIONAL(USE_EXIF, false)
AM_CONDITIONAL(USE_RESINDVD, false)
AM_CONDITIONAL(USE_FAAC, false)
AM_CONDITIONAL(USE_FAAD, false)
AM_CONDITIONAL(USE_FBDEV, false)
AM_CONDITIONAL(USE_FDK_AAC, false)
AM_CONDITIONAL(USE_FLITE, false)
AM_CONDITIONAL(USE_FLUIDSYNTH, false)
AM_CONDITIONAL(USE_GL, false)
AM_CONDITIONAL(USE_GSM, false)
AM_CONDITIONAL(USE_HLS, false)
AM_CONDITIONAL(USE_IQA, false)
AM_CONDITIONAL(USE_KATE, false)
AM_CONDITIONAL(USE_KMS, false)
AM_CONDITIONAL(USE_TIGER, false)
AM_CONDITIONAL(USE_LADSPA, false)
AM_CONDITIONAL(USE_LCMS2, false)
AM_CONDITIONAL(USE_LV2, false)
AM_CONDITIONAL(USE_LIBDE265, false)
AM_CONDITIONAL(USE_LIBMMS, false)
AM_CONDITIONAL(USE_MODPLUG, false)
AM_CONDITIONAL(USE_MPEG2ENC, false)
AM_CONDITIONAL(USE_MPLEX, false)
AM_CONDITIONAL(USE_MUSEPACK, false)
AM_CONDITIONAL(USE_NEON, false)
AM_CONDITIONAL(USE_OFA, false)
AM_CONDITIONAL(USE_OPENAL, false)
AM_CONDITIONAL(USE_OPENCV, false)
AM_CONDITIONAL(USE_OPENEXR, false)
AM_CONDITIONAL(USE_OPENGL, false)
AM_CONDITIONAL(USE_OPENJPEG, false)
AM_CONDITIONAL(USE_OPENMPT, false)
AM_CONDITIONAL(USE_OPENNI2, false)
AM_CONDITIONAL(USE_OPUS, false)
AM_CONDITIONAL(USE_WILDMIDI, false)
AM_CONDITIONAL(USE_SMOOTHSTREAMING, false)
AM_CONDITIONAL(USE_SNDFILE, false)
AM_CONDITIONAL(USE_SOUNDTOUCH, false)
AM_CONDITIONAL(USE_SPANDSP, false)
AM_CONDITIONAL(USE_SPC, false)
AM_CONDITIONAL(USE_SRTP, false)
AM_CONDITIONAL(USE_SRT, false)
AM_CONDITIONAL(USE_GME, false)
AM_CONDITIONAL(USE_DVB, false)
AM_CONDITIONAL(USE_ACM, false)
AM_CONDITIONAL(USE_VDPAU, false)
AM_CONDITIONAL(USE_SBC, false)
AM_CONDITIONAL(USE_ZBAR, false)
AM_CONDITIONAL(USE_RSVG, false)
AM_CONDITIONAL(USE_RTMP, false)
AM_CONDITIONAL(USE_TELETEXTDEC, false)
AM_CONDITIONAL(USE_UVCH264, false)
AM_CONDITIONAL(USE_WEBP, false)
AM_CONDITIONAL(USE_WEBRTC, false)
AM_CONDITIONAL(USE_WEBRTCDSP, false)
AM_CONDITIONAL(USE_OPENH264, false)
AM_CONDITIONAL(USE_X265, false)
AM_CONDITIONAL(USE_DTLS, false)
AM_CONDITIONAL(USE_VULKAN, false)
AM_CONDITIONAL(USE_TTML, false)

fi dnl of EXT plugins

dnl *** finalize CFLAGS, LDFLAGS, LIBS

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
AC_SUBST(PROFILE_CFLAGS)

if test "x$PACKAGE_VERSION_NANO" = "x1"; then
  dnl Define _only_ during CVS (not pre-releases or releases)
  DEPRECATED_CFLAGS="-DGST_DISABLE_DEPRECATED"
else
  DEPRECATED_CFLAGS=""
fi
AC_SUBST(DEPRECATED_CFLAGS)

VISIBILITY_CFLAGS=""
AS_COMPILER_FLAG([-fvisibility=hidden], [VISIBILITY_CFLAGS="-fvisibility=hidden"])
AC_SUBST(VISIBILITY_CFLAGS)

VISIBILITY_CXXFLAGS=""
if test "x$HAVE_CXX" = "xyes"; then
  AS_CXX_COMPILER_FLAG([-fvisibility=hidden], [VISIBILITY_CXXFLAGS="-fvisibility=hidden"])
fi
AC_SUBST(VISIBILITY_CXXFLAGS)

dnl disable strict aliasing
AS_COMPILER_FLAG([-fno-strict-aliasing], [EXTRA_CFLAGS="-fno-strict-aliasing"])
AC_SUBST(EXTRA_CFLAGS)

dnl every flag in GST_OPTION_CFLAGS and GST_OPTION_CXXFLAGS can be overridden
dnl at make time with e.g. make ERROR_CFLAGS=""
GST_OPTION_CFLAGS="\$(WARNING_CFLAGS) \$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
GST_OPTION_CXXFLAGS="\$(WARNING_CXXFLAGS) \$(ERROR_CXXFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
GST_OPTION_OBJCFLAGS="\$(WARNING_OBJCFLAGS) \$(ERROR_OBJCFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
AC_SUBST(GST_OPTION_CFLAGS)
AC_SUBST(GST_OPTION_CXXFLAGS)
AC_SUBST(GST_OPTION_OBJCFLAGS)

dnl FIXME: do we want to rename to GST_ALL_* ?
dnl prefer internal headers to already installed ones
dnl also add builddir include for enumtypes and marshal
dnl add GST_OPTION_CFLAGS, but overridable
GST_CFLAGS="$GST_CFLAGS $GST_STATIC_CFLAGS -DGST_USE_UNSTABLE_API"
GST_CXXFLAGS="$GST_CFLAGS $EXTRA_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CXXFLAGS) \$(VISIBILITY_CXXFLAGS)"
GST_OBJCFLAGS="$GST_CFLAGS $EXTRA_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_OBJCFLAGS) \$(VISIBILITY_CFLAGS)"
GST_CFLAGS="$GST_CFLAGS $EXTRA_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CFLAGS) \$(VISIBILITY_CFLAGS)"
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_CXXFLAGS)
AC_SUBST(GST_OBJCFLAGS)
AC_SUBST(GST_LIBS)
GST_PLUGINS_BAD_CXXFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs"
GST_PLUGINS_BAD_OBJCFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs"
GST_PLUGINS_BAD_CFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs"
AC_SUBST(GST_PLUGINS_BAD_CFLAGS)
AC_SUBST(GST_PLUGINS_BAD_CXXFLAGS)
AC_SUBST(GST_PLUGINS_BAD_OBJCFLAGS)

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
GST_LIB_LDFLAGS=""
AC_SUBST(GST_LIB_LDFLAGS)

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl *** output files ***

$MKDIR_P tests/check/orc

dnl po/Makefile.in

AC_CONFIG_FILES(
Makefile
common/Makefile
common/m4/Makefile
gst/Makefile
gst/accurip/Makefile
gst/adpcmdec/Makefile
gst/adpcmenc/Makefile
gst/aiff/Makefile
gst/videoframe_audiolevel/Makefile
gst/asfmux/Makefile
gst/audiobuffersplit/Makefile
gst/audiofxbad/Makefile
gst/audiolatency/Makefile
gst/audiomixmatrix/Makefile
gst/audiovisualizers/Makefile
gst/autoconvert/Makefile
gst/bayer/Makefile
gst/camerabin2/Makefile
gst/coloreffects/Makefile
gst/debugutils/Makefile
gst/dvbsuboverlay/Makefile
gst/dvdspu/Makefile
gst/faceoverlay/Makefile
gst/festival/Makefile
gst/fieldanalysis/Makefile
gst/freeverb/Makefile
gst/frei0r/Makefile
gst/gaudieffects/Makefile
gst/geometrictransform/Makefile
gst/gdp/Makefile
gst/id3tag/Makefile
gst/inter/Makefile
gst/interlace/Makefile
gst/ivfparse/Makefile
gst/ivtc/Makefile
gst/jp2kdecimator/Makefile
gst/jpegformat/Makefile
gst/librfb/Makefile
gst/midi/Makefile
gst/mpegdemux/Makefile
gst/mpegtsdemux/Makefile
gst/mpegtsmux/Makefile
gst/mpegtsmux/tsmux/Makefile
gst/mpegpsmux/Makefile
gst/mxf/Makefile
gst/netsim/Makefile
gst/onvif/Makefile
gst/pcapparse/Makefile
gst/pnm/Makefile
gst/proxy/Makefile
gst/rawparse/Makefile
gst/removesilence/Makefile
gst/sdp/Makefile
gst/segmentclip/Makefile
gst/siren/Makefile
gst/smooth/Makefile
gst/speed/Makefile
gst/subenc/Makefile
gst/stereo/Makefile
gst/timecode/Makefile
gst/videofilters/Makefile
gst/videoparsers/Makefile
gst/videosignal/Makefile
gst/vmnc/Makefile
gst/y4m/Makefile
gst/yadif/Makefile
gst/compositor/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/adaptivedemux/Makefile
gst-libs/gst/basecamerabinsrc/Makefile
gst-libs/gst/insertbin/Makefile
gst-libs/gst/interfaces/Makefile
gst-libs/gst/isoff/Makefile
gst-libs/gst/codecparsers/Makefile
gst-libs/gst/mpegts/Makefile
gst-libs/gst/uridownloader/Makefile
gst-libs/gst/wayland/Makefile
gst-libs/gst/webrtc/Makefile
gst-libs/gst/player/Makefile
gst-libs/gst/video/Makefile
gst-libs/gst/audio/Makefile
gst-libs/gst/opencv/Makefile
sys/Makefile
sys/dshowdecwrapper/Makefile
sys/acmenc/Makefile
sys/acmmp3dec/Makefile
sys/androidmedia/Makefile
sys/applemedia/Makefile
sys/bluez/Makefile
sys/d3dvideosink/Makefile
sys/decklink/Makefile
sys/directsound/Makefile
sys/dshowsrcwrapper/Makefile
sys/dshowvideosink/Makefile
sys/dvb/Makefile
sys/fbdev/Makefile
sys/ipcpipeline/Makefile
sys/kms/Makefile
sys/msdk/Makefile
sys/nvdec/Makefile
sys/nvenc/Makefile
sys/opensles/Makefile
sys/shm/Makefile
sys/tinyalsa/Makefile
sys/uvch264/Makefile
sys/vcd/Makefile
sys/vdpau/Makefile
sys/wasapi/Makefile
sys/winks/Makefile
sys/winscreencap/Makefile
tests/Makefile
tests/check/Makefile
tests/files/Makefile
tests/examples/Makefile
tests/examples/avsamplesink/Makefile
tests/examples/camerabin2/Makefile
tests/examples/codecparsers/Makefile
tests/examples/compositor/Makefile
tests/examples/directfb/Makefile
tests/examples/audiomixmatrix/Makefile
tests/examples/ipcpipeline/Makefile
tests/examples/mpegts/Makefile
tests/examples/mxf/Makefile
tests/examples/opencv/Makefile
tests/examples/uvch264/Makefile
tests/examples/waylandsink/Makefile
tests/examples/webrtc/Makefile
tests/icles/Makefile
ext/voamrwbenc/Makefile
ext/voaacenc/Makefile
ext/assrender/Makefile
ext/aom/Makefile
ext/bs2b/Makefile
ext/bz2/Makefile
ext/chromaprint/Makefile
ext/colormanagement/Makefile
ext/curl/Makefile
ext/dash/Makefile
ext/dc1394/Makefile
ext/directfb/Makefile
ext/wayland/Makefile
ext/daala/Makefile
ext/dts/Makefile
ext/faac/Makefile
ext/faad/Makefile
ext/fdkaac/Makefile
ext/flite/Makefile
ext/fluidsynth/Makefile
ext/gl/Makefile
ext/gsm/Makefile
ext/hls/Makefile
ext/iqa/Makefile
ext/kate/Makefile
ext/ladspa/Makefile
ext/lv2/Makefile
ext/libde265/Makefile
ext/libmms/Makefile
ext/Makefile
ext/modplug/Makefile
ext/mpeg2enc/Makefile
ext/mplex/Makefile
ext/musepack/Makefile
ext/neon/Makefile
ext/ofa/Makefile
ext/openal/Makefile
ext/opencv/Makefile
ext/openexr/Makefile
ext/openh264/Makefile
ext/openjpeg/Makefile
ext/openmpt/Makefile
ext/openni2/Makefile
ext/opus/Makefile
ext/rsvg/Makefile
ext/resindvd/Makefile
ext/rtmp/Makefile
ext/sbc/Makefile
ext/smoothstreaming/Makefile
ext/sndfile/Makefile
ext/soundtouch/Makefile
ext/spandsp/Makefile
ext/srt/Makefile
ext/srtp/Makefile
ext/teletextdec/Makefile
ext/gme/Makefile
ext/spc/Makefile
ext/wildmidi/Makefile
ext/vulkan/Makefile
ext/vulkan/xcb/Makefile
ext/vulkan/wayland/Makefile
ext/webp/Makefile
ext/x265/Makefile
ext/zbar/Makefile
ext/dtls/Makefile
ext/webrtc/Makefile
ext/webrtcdsp/Makefile
ext/ttml/Makefile
po/Makefile.in
docs/Makefile
docs/plugins/Makefile
docs/libs/Makefile
docs/version.entities
pkgconfig/Makefile
pkgconfig/gstreamer-plugins-bad.pc
pkgconfig/gstreamer-plugins-bad-uninstalled.pc
pkgconfig/gstreamer-codecparsers.pc
pkgconfig/gstreamer-codecparsers-uninstalled.pc
pkgconfig/gstreamer-insertbin.pc
pkgconfig/gstreamer-insertbin-uninstalled.pc
pkgconfig/gstreamer-mpegts.pc
pkgconfig/gstreamer-mpegts-uninstalled.pc
pkgconfig/gstreamer-player.pc
pkgconfig/gstreamer-player-uninstalled.pc
pkgconfig/gstreamer-wayland.pc
pkgconfig/gstreamer-wayland-uninstalled.pc
pkgconfig/gstreamer-webrtc.pc
pkgconfig/gstreamer-webrtc-uninstalled.pc
pkgconfig/gstreamer-bad-video.pc
pkgconfig/gstreamer-bad-video-uninstalled.pc
pkgconfig/gstreamer-bad-audio.pc
pkgconfig/gstreamer-bad-audio-uninstalled.pc
tools/Makefile
m4/Makefile
)

AC_OUTPUT

AG_GST_OUTPUT_PLUGINS
ORC_OUTPUT

