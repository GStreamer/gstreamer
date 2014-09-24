AC_PREREQ([2.68])

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT([GStreamer Bad Plug-ins],[1.4.3],[http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer],[gst-plugins-bad])

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability 1.11 no-dist-gzip dist-xz tar-ustar subdir-objects])

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
AS_LIBTOOL(GST, 403, 0, 403)

dnl *** required versions of GStreamer stuff ***
GST_REQ=1.4.0
GSTPB_REQ=1.4.0

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

if test "x$HAVE_PTHREAD_H" != "xyes"; then
  GST_PLUGINS_SELECTED=`echo $GST_PLUGINS_SELECTED | $SED -e s/dccp//`
fi

dnl *** checks for types/defines ***

dnl Check for FIONREAD ioctl declaration
dnl used in gst/dccp
GST_CHECK_FIONREAD

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***
AC_CHECK_FUNCS([gmtime_r])

dnl *** checks for headers ***
AC_CHECK_HEADERS([sys/utsname.h])

dnl *** checks for dependency libraries ***

dnl *** checks for socket and nsl libraries ***
AC_CHECK_FUNC(socket,,[AC_CHECK_LIB(socket,socket)])
AC_CHECK_FUNC(gethostbyname,,[AC_CHECK_LIB(nsl,gethostbyname)])

dnl GLib is required
GLIB_REQ=2.32.0
AG_GST_GLIB_CHECK([$GLIB_REQ])

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_BASE($GST_API_VERSION, [$GST_REQ], yes)
AG_GST_CHECK_GST_CHECK($GST_API_VERSION, [$GST_REQ], no)
AG_GST_CHECK_GST_CONTROLLER($GST_API_VERSION, [$GST_REQ], yes)
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


dnl GTK is optional and used in examples
HAVE_GTK3=NO
if test "x$BUILD_EXAMPLES" = "xyes"; then
  PKG_CHECK_MODULES(GTK3, gtk+-3.0 >= 3.4, HAVE_GTK3=yes, HAVE_GTK3=no)
  if test "x$HAVE_GTK3" = "xyes"; then
    HAVE_GTK=yes
    GTK_VERSION=`$PKG_CONFIG --variable=gtk_binary_version gtk+-3.0`
    AC_SUBST(GTK_VERSION)
    GTK_PREFIX=`$PKG_CONFIG --variable=prefix gdk-pixbuf-2.0`
    AC_SUBST(GTK_BASE_DIR)
  fi
fi
AC_SUBST(GTK3_LIBS)
AC_SUBST(GTK3_CFLAGS)
AC_SUBST(HAVE_GTK3)
AM_CONDITIONAL(HAVE_GTK3, test "x$HAVE_GTK3" = "xyes")

dnl clutter is optional and used in examples
HAVE_CLUTTER=no
HAVE_CLUTTER_X11=no
HAVE_CLUTTER_GLX=no
if test "x$BUILD_EXAMPLES" = "xyes"; then
  PKG_CHECK_MODULES(CLUTTER, clutter-1.0 >= 1.8, HAVE_CLUTTER=yes, HAVE_CLUTTER=no)
  AC_SUBST(CLUTTER_LIBS)
  AC_SUBST(CLUTTER_CFLAGS)
  AC_SUBST(HAVE_CLUTTER)

  PKG_CHECK_MODULES(CLUTTER_GLX, clutter-glx-1.0 >= 1.8, HAVE_CLUTTER_GLX=yes, HAVE_CLUTTER_GLX=no)
  AC_SUBST(CLUTTER_GLX_LIBS)
  AC_SUBST(CLUTTER_GLX_CFLAGS)
  AC_SUBST(HAVE_CLUTTER_GLX)

  PKG_CHECK_MODULES(CLUTTER_X11, clutter-x11-1.0 >= 1.8, HAVE_CLUTTER_X11=yes, HAVE_CLUTTER_X11=no)
  AC_SUBST(CLUTTER_X11_LIBS)
  AC_SUBST(CLUTTER_X11_CFLAGS)
  AC_SUBST(HAVE_CLUTTER_X11)
fi
AM_CONDITIONAL(HAVE_CLUTTER, test "x$HAVE_CLUTTER" = "xyes")
AM_CONDITIONAL(HAVE_CLUTTER_GLX, test "x$HAVE_CLUTTER_GLX" = "xyes")
AM_CONDITIONAL(HAVE_CLUTTER_X11, test "x$HAVE_CLUTTER_X11" = "xyes")

dnl used in the gl/clutteractor example
if test "x$BUILD_EXAMPLES" = "xyes"; then
  PKG_CHECK_MODULES(XCOMPOSITE, xcomposite, HAVE_XCOMPOSITE=yes, HAVE_XCOMPOSITE=no)
  AC_SUBST(XCOMPOSITE_CFLAGS)
  AC_SUBST(XCOMPOSITE_LIBS)
fi
AM_CONDITIONAL(HAVE_XCOMPOSITE, test "x$HAVE_XCOMPOSITE" = "xyes")

dnl graphene-1.0 is optional and used in gltransformation
HAVE_GRAPHENE=NO
PKG_CHECK_MODULES(GRAPHENE, graphene-1.0, HAVE_GRAPHENE=yes, HAVE_GRAPHENE=no)
if test "x$HAVE_GRAPHENE" = "xyes"; then
  AC_DEFINE(HAVE_GRAPHENE, [1] , [Use graphene])
fi
AC_SUBST(HAVE_GRAPHENE)
AC_SUBST(GRAPHENE_LIBS)
AC_SUBST(GRAPHENE_CFLAGS)
AM_CONDITIONAL(HAVE_GRAPHENE, test "x$HAVE_GRAPHENE" = "xyes")


dnl sdl is optional and used in examples
HAVE_SDL=NO
if test "x$BUILD_EXAMPLES" = "xyes"; then
  PKG_CHECK_MODULES(SDL, sdl >= 1.2.0, HAVE_SDL=yes, HAVE_SDL=no)
  AC_SUBST(SDL_LIBS)
  AC_SUBST(SDL_CFLAGS)
  AC_SUBST(SDL_CLUTTER)
fi
AM_CONDITIONAL(HAVE_SDL, test "x$HAVE_SDL" = "xyes")


dnl Needed for GtkBuilder to autoconnect signals
PKG_CHECK_MODULES(GMODULE_EXPORT, gmodule-export-2.0, HAVE_GMODULE_EXPORT=yes, HAVE_GMODULE_EXPORT=no)

dnl Needed by plugins that use g_module_*() API
PKG_CHECK_MODULES(GMODULE_NO_EXPORT, gmodule-no-export-2.0)

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
dnl Non ported plugins (non-dependant, then dependant)
dnl Make sure you have a space before and after all plugins
GST_PLUGINS_NONPORTED=" cdxaparse \
 dccp faceoverlay \
 hdvparse \
 mve mythtv nuvdemux \
 patchdetect real \
 sdi tta \
 videomeasure \
 linsys vcd \
 apexsink dc1394 \
 gsettings \
 musepack nas sdl timidity \
 directdraw acm wininet \
 xvid lv2 teletextdec sndio osx_video quicktime libvisual"
AC_SUBST(GST_PLUGINS_NONPORTED)

dnl these are all the gst plug-ins, compilable without additional libs
AG_GST_CHECK_PLUGIN(accurip)
AG_GST_CHECK_PLUGIN(adpcmdec)
AG_GST_CHECK_PLUGIN(adpcmenc)
AG_GST_CHECK_PLUGIN(aiff)
AG_GST_CHECK_PLUGIN(asfmux)
AG_GST_CHECK_PLUGIN(audiofxbad)
AG_GST_CHECK_PLUGIN(audiomixer)
AG_GST_CHECK_PLUGIN(compositor)
AG_GST_CHECK_PLUGIN(audiovisualizers)
AG_GST_CHECK_PLUGIN(autoconvert)
AG_GST_CHECK_PLUGIN(bayer)
AG_GST_CHECK_PLUGIN(camerabin2)
AG_GST_CHECK_PLUGIN(cdxaparse)
AG_GST_CHECK_PLUGIN(coloreffects)
AG_GST_CHECK_PLUGIN(dataurisrc)
AG_GST_CHECK_PLUGIN(dccp)
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
AG_GST_CHECK_PLUGIN(hdvparse)
AG_GST_CHECK_PLUGIN(id3tag)
AG_GST_CHECK_PLUGIN(inter)
AG_GST_CHECK_PLUGIN(interlace)
AG_GST_CHECK_PLUGIN(ivfparse)
AG_GST_CHECK_PLUGIN(ivtc)
AG_GST_CHECK_PLUGIN(jp2kdecimator)
AG_GST_CHECK_PLUGIN(jpegformat)
AG_GST_CHECK_PLUGIN(librfb)
AG_GST_CHECK_PLUGIN(liveadder)
AG_GST_CHECK_PLUGIN(midi)
AG_GST_CHECK_PLUGIN(mpegdemux)
AG_GST_CHECK_PLUGIN(mpegtsdemux)
AG_GST_CHECK_PLUGIN(mpegtsmux)
AG_GST_CHECK_PLUGIN(mpegpsmux)
AG_GST_CHECK_PLUGIN(mve)
AG_GST_CHECK_PLUGIN(mxf)
AG_GST_CHECK_PLUGIN(nuvdemux)
AG_GST_CHECK_PLUGIN(patchdetect)
AG_GST_CHECK_PLUGIN(pcapparse)
AG_GST_CHECK_PLUGIN(pnm)
AG_GST_CHECK_PLUGIN(rawparse)
AG_GST_CHECK_PLUGIN(real)
AG_GST_CHECK_PLUGIN(removesilence)
AG_GST_CHECK_PLUGIN(sdi)
AG_GST_CHECK_PLUGIN(sdp)
AG_GST_CHECK_PLUGIN(segmentclip)
AG_GST_CHECK_PLUGIN(siren)
AG_GST_CHECK_PLUGIN(smooth)
AG_GST_CHECK_PLUGIN(speed)
AG_GST_CHECK_PLUGIN(subenc)
AG_GST_CHECK_PLUGIN(stereo)
AG_GST_CHECK_PLUGIN(tta)
AG_GST_CHECK_PLUGIN(videofilters)
AG_GST_CHECK_PLUGIN(videomeasure)
AG_GST_CHECK_PLUGIN(videoparsers)
AG_GST_CHECK_PLUGIN(videosignal)
AG_GST_CHECK_PLUGIN(vmnc)
AG_GST_CHECK_PLUGIN(y4m)
AG_GST_CHECK_PLUGIN(yadif)

dnl *** plug-ins to exclude ***

dnl real plugin only works on i386 and x86_64 for the time being.
if test "x$HAVE_CPU_I386" != "xyes" && test "x$HAVE_CPU_X86_64" != "xyes"; then
  AC_MSG_WARN([Not building real plugin, only works on 32bit and 64bit x86 platforms])
  AG_GST_DISABLE_PLUGIN(real)
fi

AC_CHECK_HEADER(AVFoundation/AVFoundation.h, HAVE_AVFOUNDATION="yes", HAVE_AVFOUNDATION="no", [-])
AC_CHECK_HEADER(MobileCoreServices/MobileCoreServices.h, HAVE_IOS="yes", HAVE_IOS="no", [-])
AC_CHECK_HEADER(VideoToolbox/VideoToolbox.h, HAVE_VIDEOTOOLBOX="yes", HAVE_VIDEOTOOLBOX="no", [-])

AM_CONDITIONAL(HAVE_AVFOUNDATION, test "x$HAVE_AVFOUNDATION" = "xyes")
if test "x$HAVE_AVFOUNDATION" = "xyes"; then
  AC_DEFINE(HAVE_AVFOUNDATION, 1, [Define if building with AVFoundation])
fi

AM_CONDITIONAL(HAVE_IOS, test "x$HAVE_IOS" = "xyes")
if test "x$HAVE_IOS" = "xyes"; then
  AC_DEFINE(HAVE_IOS, 1, [Define if building for Apple iOS])
fi

if test "x$HAVE_IOS" = "xyes"; then
  AC_MSG_CHECKING([Checking for __IPHONE_OS_VERSION_MIN_REQUIRED >= __IPHONE_8_0])

  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <AvailabilityMacros.h>]], [[
        #if !defined(__IPHONE_OS_VERSION_MIN_REQUIRED) || __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_8_0
        #error "Need iOS >= 8.0 for VideoToolbox"
        #endif
  ]])], [ AC_MSG_RESULT(yes)
    HAVE_VIDEOTOOLBOX="yes"
  ], [ AC_MSG_RESULT(no)
    HAVE_VIDEOTOOLBOX="no"
  ])
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

if test "x$HAVE_PTHREAD_H" = "xyes"; then
  DCCP_LIBS="$DCCP_LIBS -lpthread"
  AC_SUBST(DCCP_LIBS)
else
  AG_GST_DISABLE_PLUGIN(dccp)
fi

dnl *** opengl ***
AC_ARG_ENABLE([opengl],
     [  --enable-opengl         Enable Desktop OpenGL support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_GL=yes ;;
       no)   NEED_GL=no ;;
       auto) NEED_GL=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-opengl]) ;;
     esac],[NEED_GL=auto])

AC_ARG_WITH([opengl-module-name],
  AS_HELP_STRING([--with-opengl-module-name],[library module name for OpenGL (default: libGL)]))
if test x$with_opengl_module_name != x; then
  AC_DEFINE_UNQUOTED(GST_GL_LIBGL_MODULE_NAME, "$with_opengl_module_name", [OpenGL module name])
fi

AC_ARG_ENABLE([gles2],
     [  --enable-gles2          Enable OpenGL|ES 2.0 support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_GLES2=yes ;;
       no)   NEED_GLES2=no ;;
       auto) NEED_GLES2=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-gles2]) ;;
     esac],[NEED_GLES2=auto])

AC_ARG_WITH([gles2-module-name],
  AS_HELP_STRING([--with-gles2-module-name],[library module name for GLES2 (default: libGLESv2)]))
if test x$with_gles2_module_name != x; then
  AC_DEFINE_UNQUOTED(GST_GL_LIBGLESV2_MODULE_NAME, "$with_gles2_module_name", [GLES2 module name])
fi

AC_ARG_ENABLE([egl],
     [  --enable-egl            Enable EGL support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_EGL=yes ;;
       no)   NEED_EGL=no ;;
       auto) NEED_EGL=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-egl]) ;;
     esac],[NEED_EGL=auto])

AC_ARG_WITH([egl-module-name],
  AS_HELP_STRING([--with-egl-module-name],[library module name for EGL (default: libEGL)]))
if test x$with_egl_module_name != x; then
  AC_DEFINE_UNQUOTED(GST_GL_LIBEGL_MODULE_NAME, "$with_egl_module_name", [EGL module name])
fi

AC_ARG_ENABLE([wgl],
     [  --enable-wgl            Enable WGL support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_WGL=yes ;;
       no)   NEED_WGL=no ;;
       auto) NEED_WGL=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-wgl]) ;;
     esac],[NEED_WGL=auto])

AC_ARG_ENABLE([glx],
     [  --enable-glx            Enable GLX support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_GLX=yes ;;
       no)   NEED_GLX=no ;;
       auto) NEED_GLX=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-glx]) ;;
     esac],[NEED_GLX=auto])

AC_ARG_ENABLE([cocoa],
     [  --enable-cocoa          Enable Cocoa support (using GNUstep on non OS X platforms) @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_COCOA=yes ;;
       no)   NEED_COCOA=no ;;
       auto) NEED_COCOA=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-cocoa]) ;;
     esac],[NEED_COCOA=auto])

AC_ARG_ENABLE([x11],
     [  --enable-x11            Enable x11 support @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_X11=yes ;;
       no)   NEED_X11=no ;;
       auto) NEED_X11=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-x11]) ;;
     esac],[NEED_X11=auto])

AC_ARG_ENABLE([wayland],
     [  --enable-wayland        Enable Wayland support (requires EGL) @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_WAYLAND_EGL=yes ;;
       no)   NEED_WAYLAND_EGL=no ;;
       auto) NEED_WAYLAND_EGL=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-wayland]) ;;
     esac],[NEED_WAYLAND_EGL=auto])

AC_ARG_ENABLE([dispmanx],
     [  --enable-dispmanx        Enable Dispmanx support (requires EGL) @<:@default=auto@:>@],
     [case "${enableval}" in
       yes)  NEED_DISPMANX=yes ;;
       no)   NEED_DISPMANX=no ;;
       auto) NEED_DISPMANX=auto ;;
       *) AC_MSG_ERROR([bad value ${enableval} for --enable-dispmanx]) ;;
     esac],[NEED_DISPMANX=auto])

AG_GST_CHECK_X
save_CPPFLAGS="$CPPFLAGS"
save_LIBS="$LIBS"

HAVE_GL=no
HAVE_GLES2=no
HAVE_GLU=no
HAVE_GNUSTEP_COCOA=no
HAVE_WAYLAND_EGL=no

HAVE_EGL_RPI=no

case $host in
  *-mingw32* )
    LIBS="$LIBS -lgdi32"
    AG_GST_CHECK_LIBHEADER(GL, opengl32, glTexImage2D,, GL/gl.h)
    AG_GST_CHECK_LIBHEADER(GLU, glu32, gluSphere,, GL/glu.h)
    AC_CHECK_HEADER(GL/wglext.h, HAVE_WGLEXT="yes", HAVE_WGLEXT="no", [#include <GL/gl.h>])
    if test "x$HAVE_WGLEXT" = "xyes"; then
      HAVE_WGL=yes
      HAVE_GL=yes
      HAVE_GLU=yes
    fi
  ;;
  *)
    AG_GST_CHECK_LIBHEADER(GL, GL, glTexImage2D,, GL/gl.h)
    AG_GST_CHECK_LIBHEADER(GLU, GLU, gluSphere,, GL/glu.h)
    AG_GST_CHECK_LIBHEADER(GLES2, GLESv2, glTexImage2D,, GLES2/gl2.h)
    AG_GST_CHECK_LIBHEADER(EGL, EGL, eglGetError,, EGL/egl.h)

    old_LIBS=$LIBS
    old_CFLAGS=$CFLAGS

    dnl FIXME: Mali EGL depends on GLESv1 or GLESv2
    AC_CHECK_HEADER([EGL/fbdev_window.h],
      [
        LIBS="$LIBS -lUMP"
        AC_CHECK_LIB([Mali], [mali_image_create],
          [
            LIBS="$LIBS -lMali"
            AC_CHECK_LIB([GLESv2], [glEnable],
              [
                AC_CHECK_HEADER([GLES2/gl2.h],
                  [
                    AC_CHECK_LIB([EGL], [eglGetProcAddress],
                      [
                        AC_CHECK_HEADER([EGL/egl.h],
                          [
                            HAVE_EGL=yes
                            HAVE_GLES2=yes
                            EGL_LIBS="-lMali -lUMP"
                            EGL_CFLAGS=""
                            AC_DEFINE(USE_EGL_MALI_FB, [1], [Use Mali FB EGL platform])
                          ])
                      ])
                  ])
              ])
          ])
      ])

    dnl FIXME: EGL of RPi depends on GLESv1 or GLESv2
    dnl FIXME: GLESv2 of RPi depends on EGL... WTF!
    LIBS="$LIBS -lvcos -lvchiq_arm"
    AC_CHECK_LIB([bcm_host], [bcm_host_init],
      [
        LIBS="$LIBS -lbcm_host"
        AC_CHECK_HEADER(bcm_host.h,
          [
            LIBS="$LIBS -lGLESv2"
            AC_CHECK_LIB([EGL], [eglGetProcAddress],
              [
                LIBS="$LIBS -lEGL"
                AC_CHECK_HEADER([EGL/egl.h],
                  [
                    AC_CHECK_LIB([GLESv2], [glEnable],
                      [
                        AC_CHECK_HEADER([GLES2/gl2.h],
                          [
                            HAVE_EGL=yes
                            HAVE_GLES2=yes
                            HAVE_EGL_RPI=yes
                            EGL_LIBS="-lbcm_host -lvcos -lvchiq_arm"
                            EGL_CFLAGS=""
                            AC_DEFINE(USE_EGL_RPI, [1], [Use RPi platform])
                          ])
                      ])
                  ])
              ])
          ])
      ])

    LIBS=$old_LIBS
    CFLAGS=$old_CFLAGS

    PKG_CHECK_MODULES(WAYLAND_EGL, wayland-client >= 1.0 wayland-cursor >= 1.0 wayland-egl >= 9.0, HAVE_WAYLAND_EGL=yes, HAVE_WAYLAND_EGL=no)
  ;;
esac

case $host in
  *-darwin* ) ;;
  *)
    AC_PATH_PROG([GNUSTEP_CONFIG],[gnustep-config])
    if test "x$GNUSTEP_CONFIG" != "x"; then
      GNUSTEP_HOST="`$GNUSTEP_CONFIG --variable=GNUSTEP_HOST`"
      case $host in
        *-mingw* )
          case $GNUSTEP_HOST in
            *-mingw* )
              HAVE_GNUSTEP_COCOA=yes
            ;;
          esac
          ;;
        * )
          HAVE_GNUSTEP_COCOA=yes
        ;;
      esac
    fi
  ;;
esac

AC_MSG_CHECKING([for GNUstep])
if test "x$HAVE_GNUSTEP_COCOA" = "xyes" ; then
  AC_MSG_RESULT([yes])
else
  AC_MSG_RESULT([no])
fi

CPPFLAGS="$save_CPPFLAGS"
LIBS="$save_LIBS"

AC_SUBST(HAVE_GL)
AC_SUBST(HAVE_GLU)
AC_SUBST(HAVE_GLES2)

USE_OPENGL=no
USE_GLES2=no
USE_GLX=no
USE_COCOA=no
USE_WGL=no
USE_X11=no
USE_EAGL=no
GL_LIBS=
GL_CFLAGS=
GL_OBJCFLAGS=

dnl Check for what the user asked for and what we could find
if test "x$HAVE_EGL" = "xno"; then
  if test "x$NEED_EGL" = "xyes"; then
    AC_MSG_ERROR([Could not find the required EGL libraries])
  fi
fi

if test "x$HAVE_GL" = "xno"; then
  if test "x$NEED_GL" = "xyes"; then
    AC_MSG_ERROR([Could not find the required OpenGL libraries])
  fi
fi

if test "x$HAVE_GLES2" = "xno"; then
  if test "x$NEED_GLES2" = "xyes"; then
    AC_MSG_ERROR([Could not find the required OpenGL|ES 2.0 libraries])
  fi
fi

dnl X, GLX and OpenGL
if test "x$HAVE_X" = "xno"; then
  if test "x$NEED_GLX" = "xyes"; then
    AC_MSG_ERROR([Building the GLX backend without X11 is unsupported])
  fi
  if test "x$NEED_X11" = "xyes"; then
    AC_MSG_ERROR([Could not find X11 development libraries])
  fi
else
  if test "x$NEED_GL" != "xno"; then
    if test "x$HAVE_GL" = "xno"; then
      if test "x$NEED_GLX" = "xyes"; then
        AC_MSG_ERROR([Building the GLX backend without the OpenGL backend is unsupported])
      fi
    else dnl HAVE_GL=yes
      if test "x$HAVE_GLU" = "xno"; then
        if test "x$NEED_GL" = "xyes"; then
          AC_MSG_ERROR([GLU is required with OpenGL support])
        else
          AC_MSG_WARN([GLU is required with OpenGL support])
        fi
      else dnl HAVE_GLU=yes
        USE_OPENGL=yes
        if test "x$NEED_GLX" != "xno"; then
          USE_GLX=yes
        fi
      fi
    fi
  fi
fi

dnl check if we can include both GL and GLES2 at the same time
if test "x$HAVE_GL" = "xyes" -a "x$HAVE_GLES2" = "xyes"; then
  GL_INCLUDES="
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
# ifdef __APPLE__
#  include <OpenGLES/ES2/gl.h>
#  include <OpenGLES/ES2/glext.h>
# else
#  include <GLES2/gl2.h>
#  include <GLES2/gl2ext.h>
# endif
# if __APPLE__
#  include <OpenGL/OpenGL.h>
#  include <OpenGL/gl.h>
#  include <OpenGL/glu.h>
# else
#  include <GL/gl.h>
#  include <GL/glu.h>
#  if __WIN32__ || _WIN32
#   include <GL/glext.h>
#  endif
# endif
"
  AC_MSG_CHECKING([whether it is possible to include both GL and GLES2 headers])
  save_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $ERROR_CFLAGS"
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[$GL_INCLUDES]], [[
    #if !defined(GL_FALSE)
    #error Failed to include GL headers
    #endif
    ]])],[ AC_MSG_RESULT(yes)
    ],[AC_MSG_RESULT(no)
    if test "x$NEED_GLES2" = "xyes"; then
      if test "x$NEED_OPENGL" = "xyes"; then
        AC_MSG_ERROR([Cannot seem to include both GL and GLES2 headers. Try disabling one API])
      fi
      AC_MSG_WARN([Disabling Desktop GL support])
      HAVE_OPENGL=no
    else
      AC_MSG_WARN([Disabling GL|ES 2.0 support])
      HAVE_GLES2=no
    fi
  ])
  CPPFLAGS="$save_CPPFLAGS"
fi

#dnl Check for OpenGL, GLU
echo host is $host
case $host in
  *-android*)
    if test "x$NEED_WGL" = "xyes"; then
      AC_MSG_ERROR([WGL is not available on Android])
    fi
    if test "x$NEED_GLX" = "xyes"; then
      AC_MSG_ERROR([GLX is not available on Android])
    fi
    if test "x$NEED_GL" = "xyes"; then
      AC_MSG_ERROR([GL is not available on Android])
    fi
    if test "x$NEED_X11" = "xyes"; then
      AC_MSG_ERROR([X11 is not available on Android])
    fi
    if test "x$NEED_COCOA" = "xyes"; then
      AC_MSG_ERROR([Cocoa is not available on Android])
    fi

    dnl OpenGL|ES 2.0
    if test "x$HAVE_GLES2" = "xyes"; then
      if test "x$NEED_GLES2" != "xno"; then
        GL_LIBS="$GL_LIBS -lGLESv2"
        USE_GLES2=yes
      fi
    fi

    dnl EGL
    if test "x$HAVE_EGL" = "xyes"; then
      if test "x$NEED_EGL" != "xno"; then
        GL_LIBS="$GL_LIBS -lEGL"
        USE_EGL=yes
      fi
    fi

    if test "x$USE_EGL" != "xyes"; then
      AC_MSG_ERROR([Need EGL on Android])
    fi

    if test "x$USE_GLES2" != "xyes"; then
      AC_MSG_ERROR([Need OpenGL|ES 2.0 on Android])
    fi

    HAVE_WINDOW_ANDROID=yes
    ;;
  *-linux* | *-cygwin* | *-solaris* | *-netbsd* | *-freebsd* | *-openbsd* | *-kfreebsd* | *-dragonflybsd* | *-gnu* )
    if test "x$NEED_WGL" = "xyes"; then
      AC_MSG_ERROR([WGL is not available on unix])
    fi

    if test "x$HAVE_X" = "xno"; then
      if test "x$HAVE_WAYLAND_EGL" = "xno"; then
        AC_MSG_WARN([X or Wayland is required for OpenGL support])
      fi
    fi

    dnl check Desktop OpenGL
    if test "x$HAVE_GL" = "xyes"; then
      if test "x$NEED_GL" != "xno"; then
        GL_LIBS="$GL_LIBS -lGL"
      fi
      if test "x$HAVE_GLU" = "xyes"; then
        GL_LIBS="$GL_LIBS -lGLU"
      fi
    fi

    dnl OpenGL|ES 2.0
    if test "x$HAVE_GLES2" = "xyes"; then
      if test "x$NEED_GLES2" != "xno"; then
        GL_LIBS="$GL_LIBS -lGLESv2"
        USE_GLES2=yes
      fi
    fi

    if test "x$HAVE_X" = "xyes" -a "x$HAVE_EGL_RPI" = "xno"; then
      if test "x$NEED_X11" != "xno"; then
        GL_LIBS="$GL_LIBS $X_LIBS"
        GL_CFLAGS="$GL_CFLAGS $X_CFLAGS"
        HAVE_WINDOW_X11=yes
      fi
    fi

    if test "x$HAVE_WAYLAND_EGL" = "xyes"; then
      if test "x$NEED_EGL" = "xno" -o "x$HAVE_EGL" = "xno"; then
        AC_MSG_WARN([EGL is required by the Wayland backend for OpenGL support])
      else
        if test "x$NEED_WAYLAND_EGL" != "xno"; then
          HAVE_WINDOW_WAYLAND=yes
          GL_LIBS="$GL_LIBS $WAYLAND_EGL_LIBS"
          GL_CFLAGS="$GL_CFLAGS $WAYLAND_EGL_CFLAGS"
        fi
      fi
    fi

    if test "x$HAVE_EGL_RPI" = "xyes"; then
      if test "x$NEED_DISPMANX" != "xno"; then
        HAVE_WINDOW_DISPMANX=yes
        USE_EGL=yes
      fi
    fi

    dnl EGL
    if test "x$HAVE_EGL" = "xno"; then
      if test "x$HAVE_GL" = "xno"; then
        AC_MSG_WARN([Building requires either EGL or GLX for OpenGL support])
      fi
    else
      if test "x$NEED_EGL" != "xno"; then
        if test "x$HAVE_WINDOW_WAYLAND" = "xyes" -o "x$HAVE_WINDOW_X11" = "xyes" -o "x$HAVE_WINDOW_DISPMANX" = "xyes"; then
          GL_LIBS="$GL_LIBS -lEGL $EGL_LIBS"
          GL_CFLAGS="$GL_CFLAGS $EGL_CFLAGS"
          USE_EGL=yes
        fi
      fi
    fi

    dnl GNUstep provides the Cocoa API on unix
    if test "x$NEED_COCOA" != "xno"; then
      if test "x$HAVE_GNUSTEP_COCOA" = "xyes" ; then
        GNUSTEP_OBJCFLAGS="`$GNUSTEP_CONFIG --objc-flags`"
        GNUSTEP_LIBS="`$GNUSTEP_CONFIG --gui-libs`"
        GL_LIBS="$GL_LIBS $GNUSTEP_LIBS -lgnustep-gui -lgnustep-base"
        GL_OBJCFLAGS="$GL_OBJCFLAGS $GNUSTEP_OBJCFLAGS"

        USE_COCOA=yes
        HAVE_WINDOW_COCOA=yes
      fi
    fi
    ;;
  *-darwin*)
    if test "x$HAVE_IOS" = "xyes"; then
      if test "x$NEED_WGL" = "xyes"; then
        AC_MSG_ERROR([WGL is not available on iOS])
      fi
      if test "x$NEED_GLX" = "xyes"; then
        AC_MSG_ERROR([GLX is not available on iOS])
      fi
      if test "x$NEED_GL" = "xyes"; then
        AC_MSG_ERROR([GL is not available on iOS])
      fi
      if test "x$NEED_X11" = "xyes"; then
        AC_MSG_ERROR([X11 is not available on iOS])
      fi
      if test "x$NEED_COCOA" = "xyes"; then
        AC_MSG_ERROR([Cocoa is not available on iOS])
      fi
      if test "x$NEED_EGL" = "xyes"; then
        AC_MSG_ERROR([EGL is not available on iOS])
      fi

      GL_LIBS="$LIBS -framework OpenGLES -framework QuartzCore -framework UIKit -framework CoreGraphics -framework CoreFoundation -framework Foundation"
      GL_CFLAGS="$GL_CFLAGS"
      USE_GLES2=yes
      USE_EAGL=yes
      HAVE_WINDOW_EAGL=yes

      ac_cv_type_GLsizeiptr=yes
      ac_cv_type_GLintptr=yes
      ac_cv_type_GLchar=yes
    else
      dnl Only osx supports cocoa.
      if test "x$NEED_WGL" = "xyes"; then
        AC_MSG_ERROR([WGL is not available on Mac OS X])
      fi
      if test "x$NEED_EGL" = "xyes"; then
        AC_MSG_ERROR([EGL is not available on Mac OS X])
      fi
      if test "x$NEED_GLES2" = "xyes"; then
        AC_MSG_ERROR([OpenGL|ES 2.0 is not supported on your platform yet])
      fi

      if test "x$NEED_COCOA" != "xno"; then
        GL_LIBS="$LIBS -framework OpenGL -framework Cocoa"
        GL_CFLAGS="$GL_CFLAGS"
        USE_COCOA=yes
        HAVE_WINDOW_COCOA=yes
        USE_OPENGL=yes
      fi

      if test "x$USE_GLX" = "xyes"; then
        if test "x$HAVE_X" = "xyes"; then
          if test "x$NEED_X11" != "xno"; then
            GL_LIBS="$GL_LIBS $X_LIBS"
            GL_CFLAGS="$GL_CFLAGS $X_CFLAGS"
            HAVE_WINDOW_X11=yes
          fi
        fi

        if test "x$HAVE_GL" = "xyes"; then
          if test "x$NEED_GL" != "xno"; then
            GL_LIBS="$GL_LIBS -lGL"
          fi
          if test "x$HAVE_GLU" = "xyes"; then
            GL_LIBS="$GL_LIBS -lGLU"
          fi
          USE_OPENGL=yes
        fi
      fi
    fi
    ;;
  *-mingw32*)
    if test "x$NEED_GLX" = "xyes"; then
      AC_MSG_ERROR([GLX is not available on Windows])
    fi
    if test "x$NEED_GLES2" = "xyes"; then
      AC_MSG_ERROR([OpenGL|ES 2.0 is not supported on your platform yet])
    fi

    if test "x$HAVE_GL" = "xyes"; then
      if test "$NEED_GL" != "xno"; then
        if test "x$HAVE_WGL" = "xyes"; then
          if test "$NEED_WGL" != "xno"; then
            GL_LIBS="$GL_LIBS -lgdi32 -lopengl32 -lglu32"
            HAVE_WINDOW_WIN32=yes
            USE_OPENGL=yes
            USE_WGL=yes
          fi
        fi
      fi
    fi

    dnl GNUstep provides the Cocoa API on win32
    if test "x$HAVE_GNUSTEP_COCOA" = "xyes" ; then
      GNUSTEP_CFLAGS="`$GNUSTEP_CONFIG --objc-flags`"
      GNUSTEP_LIBS="`$GNUSTEP_CONFIG --gui-libs`"
      GL_LIBS="$GL_LIBS $GNUSTEP_LIBS -lgnustep-gui -lgnustep-base"
      GL_OBJCFLAGS="$OBJCFLAGS $GNUSTEP_CFLAGS"

      USE_COCOA=yes
      HAVE_WINDOW_COCOA=yes
    fi
    ;;
  *)
    AC_MSG_WARN([Don't know how to check for OpenGL on your platform.])
    ;;
esac

GL_PLATFORMS=
GL_WINDOWS=
GL_APIS=
GL_CONFIG_DEFINES=

dnl APIs

GST_GL_HAVE_OPENGL=0
GST_GL_HAVE_GLES2=0
GST_GL_HAVE_GLES3=0

if test "x$USE_OPENGL" = "xyes"; then
  GL_APIS="gl $GL_APIS"
  GST_GL_HAVE_OPENGL=1
fi
if test "x$USE_GLES2" = "xyes"; then
  GL_APIS="gles2 $GL_APIS"
  GST_GL_HAVE_GLES2=1
fi

GL_CONFIG_DEFINES="$GL_CONFIG_DEFINES
#define GST_GL_HAVE_OPENGL $GST_GL_HAVE_OPENGL
#define GST_GL_HAVE_GLES2 $GST_GL_HAVE_GLES2
#define GST_GL_HAVE_GLES3 $GST_GL_HAVE_GLES3
"

dnl WINDOW's

GST_GL_HAVE_WINDOW_X11=0
GST_GL_HAVE_WINDOW_COCOA=0
GST_GL_HAVE_WINDOW_WIN32=0
GST_GL_HAVE_WINDOW_WAYLAND=0
GST_GL_HAVE_WINDOW_ANDROID=0
GST_GL_HAVE_WINDOW_DISPMANX=0
GST_GL_HAVE_WINDOW_EAGL=0

if test "x$HAVE_WINDOW_X11" = "xyes"; then
  GL_WINDOWS="x11 $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_X11=1
fi
if test "x$HAVE_WINDOW_COCOA" = "xyes"; then
  GL_WINDOWS="cocoa $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_COCOA=1
fi
if test "x$HAVE_WINDOW_WIN32" = "xyes"; then
  GL_WINDOWS="win32 $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_WIN32=1
fi
if test "x$HAVE_WINDOW_WAYLAND" = "xyes"; then
  GL_WINDOWS="wayland $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_WAYLAND=1
fi
if test "x$HAVE_WINDOW_ANDROID" = "xyes"; then
  GL_WINDOWS="android $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_ANDROID=1
fi
if test "x$HAVE_WINDOW_DISPMANX" = "xyes"; then
  GL_WINDOWS="dispmanx $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_DISPMANX=1
fi
if test "x$HAVE_WINDOW_EAGL" = "xyes"; then
  GL_WINDOWS="eagl $GL_WINDOWS"
  GST_GL_HAVE_WINDOW_EAGL=1
fi

GL_CONFIG_DEFINES="$GL_CONFIG_DEFINES
#define GST_GL_HAVE_WINDOW_X11 $GST_GL_HAVE_WINDOW_X11
#define GST_GL_HAVE_WINDOW_COCOA $GST_GL_HAVE_WINDOW_COCOA
#define GST_GL_HAVE_WINDOW_WIN32 $GST_GL_HAVE_WINDOW_WIN32
#define GST_GL_HAVE_WINDOW_WAYLAND $GST_GL_HAVE_WINDOW_WAYLAND
#define GST_GL_HAVE_WINDOW_ANDROID $GST_GL_HAVE_WINDOW_ANDROID
#define GST_GL_HAVE_WINDOW_DISPMANX $GST_GL_HAVE_WINDOW_DISPMANX
#define GST_GL_HAVE_WINDOW_EAGL $GST_GL_HAVE_WINDOW_EAGL
"

dnl PLATFORM's

GST_GL_HAVE_PLATFORM_EGL=0
GST_GL_HAVE_PLATFORM_GLX=0
GST_GL_HAVE_PLATFORM_WGL=0
GST_GL_HAVE_PLATFORM_CGL=0
GST_GL_HAVE_PLATFORM_EAGL=0

if test "x$USE_EGL" = "xyes"; then
  GL_PLATFORMS="egl $GL_PLATFORMS"
  GST_GL_HAVE_PLATFORM_EGL=1
fi
if test "x$USE_GLX" = "xyes"; then
  GL_PLATFORMS="glx $GL_PLATFORMS"
  GST_GL_HAVE_PLATFORM_GLX=1
fi
if test "x$USE_WGL" = "xyes"; then
  GL_PLATFORMS="wgl $GL_PLATFORMS"
  GST_GL_HAVE_PLATFORM_WGL=1
fi
if test "x$USE_COCOA" = "xyes"; then
  GL_PLATFORMS="cgl $GL_PLATFORMS"
  GST_GL_HAVE_PLATFORM_CGL=1
fi
if test "x$USE_EAGL" = "xyes"; then
  GL_PLATFORMS="eagl $GL_PLATFORMS"
  GST_GL_HAVE_PLATFORM_EAGL=1
fi

GL_CONFIG_DEFINES="$GL_CONFIG_DEFINES
#define GST_GL_HAVE_PLATFORM_EGL $GST_GL_HAVE_PLATFORM_EGL
#define GST_GL_HAVE_PLATFORM_GLX $GST_GL_HAVE_PLATFORM_GLX
#define GST_GL_HAVE_PLATFORM_WGL $GST_GL_HAVE_PLATFORM_WGL
#define GST_GL_HAVE_PLATFORM_CGL $GST_GL_HAVE_PLATFORM_CGL
#define GST_GL_HAVE_PLATFORM_EAGL $GST_GL_HAVE_PLATFORM_EAGL
"

dnl Check for no platforms/window systems
if test "x$GL_APIS" = "x"; then
  AC_MSG_WARN([Either OpenGL or OpenGL|ES is required for OpenGL support])
fi
if test "x$GL_PLATFORMS" = "x"; then
  AC_MSG_WARN([Could not find any OpenGL platforms to use such as CGL, WGL or GLX])
fi
if test "x$GL_WINDOWS" = "x"; then
  AC_MSG_WARN([Could not find any window systems to use such as Cocoa, Win32API or X11])
fi

if test "x$GL_APIS" = "x" -o "x$GL_PLATFORMS" = "x" -o "x$GL_WINDOWS" = "x"; then
  GL_LIBS=
  GL_CFLAGS=
  GL_OBJCFLAGS=
  USE_OPENGL=no
  USE_GLES2=no
  USE_GLX=no
  USE_EGL=no
  USE_WGL=no
  USE_COCOA=no
  USE_EGL_MALI=no
  USE_EGL_RPI=no
  USE_EAGL=no

  HAVE_WINDOW_X11=no
  HAVE_WINDOW_WIN32=no
  HAVE_WINDOW_DISPMANX=no
  HAVE_WINDOW_WAYLAND=no
  HAVE_WINDOW_ANDROID=no
  HAVE_WINDOW_COCOA=no
  HAVE_WINDOW_EAGL=no
fi

AC_SUBST(GL_LIBS)
AC_SUBST(GL_CFLAGS)
AC_SUBST(GL_OBJCFLAGS)
AC_SUBST(USE_OPENGL)
AC_SUBST(USE_GLES2)

AM_CONDITIONAL(HAVE_WINDOW_X11, test "x$HAVE_WINDOW_X11" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_COCOA, test "x$HAVE_WINDOW_COCOA" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_WIN32, test "x$HAVE_WINDOW_WIN32" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_DISPMANX, test "x$HAVE_WINDOW_DISPMANX" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_WAYLAND, test "x$HAVE_WINDOW_WAYLAND" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_ANDROID, test "x$HAVE_WINDOW_ANDROID" = "xyes")
AM_CONDITIONAL(HAVE_WINDOW_EAGL, test "x$HAVE_WINDOW_EAGL" = "xyes")

AM_CONDITIONAL(USE_OPENGL, test "x$USE_OPENGL" = "xyes")
AM_CONDITIONAL(USE_GLES2, test "x$USE_GLES2" = "xyes")
AM_CONDITIONAL(USE_GLX, test "x$USE_GLX" = "xyes")
AM_CONDITIONAL(USE_EGL, test "x$USE_EGL" = "xyes")
AM_CONDITIONAL(USE_WGL, test "x$USE_WGL" = "xyes")
AM_CONDITIONAL(USE_COCOA, test "x$USE_COCOA" = "xyes")
AM_CONDITIONAL(USE_EGL_MALI, test "x$USE_EGL_MALI" = "xyes")
AM_CONDITIONAL(USE_EGL_RPI, test "x$USE_EGL_RPI" = "xyes")
AM_CONDITIONAL(USE_EAGL, test "x$USE_EAGL" = "xyes")

AM_CONDITIONAL(HAVE_GST_GL, test "x$USE_OPENGL" = "xyes" -o "x$USE_GLES2" = "xyes")

dnl Check for some types that are not always present
GL_INCLUDES=""
if test "x$USE_GLES2" = "xyes"; then
  GL_INCLUDES="$GL_INCLUDES
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
# ifdef __APPLE__
#  include <OpenGLES/ES2/gl.h>
#  include <OpenGLES/ES2/glext.h>
# else
#  include <GLES2/gl2.h>
#  include <GLES2/gl2ext.h>
# endif
"
fi

if test "x$USE_OPENGL" = "xyes"; then
  GL_INCLUDES="$GL_INCLUDES
# if __APPLE__
#  include <OpenGL/OpenGL.h>
#  include <OpenGL/gl.h>
#  include <OpenGL/glu.h>
# else
#  include <GL/gl.h>
#  include <GL/glu.h>
#  if __WIN32__ || _WIN32
#   include <GL/glext.h>
#  endif
# endif
"
fi

GST_GL_HAVE_GLEGLIMAGEOES=0
GST_GL_HAVE_GLCHAR=0
GST_GL_HAVE_GLSIZEIPTR=0
GST_GL_HAVE_GLINTPTR=0

old_CFLAGS=$CFLAGS
CFLAGS="$GL_CFLAGS $CFLAGS"

AC_CHECK_TYPES(GLeglImageOES, [], [], [[$GL_INCLUDES]])
if test "x$ac_cv_type_GLeglImageOES" = "xyes"; then
  GST_GL_HAVE_GLEGLIMAGEOES=1
fi

AC_CHECK_TYPES(GLchar, [], [], [[$GL_INCLUDES]])
if test "x$ac_cv_type_GLchar" = "xyes"; then
  GST_GL_HAVE_GLCHAR=1
fi

AC_CHECK_TYPES(GLsizeiptr, [], [], [[$GL_INCLUDES]])
if test "x$ac_cv_type_GLsizeiptr" = "xyes"; then
  GST_GL_HAVE_GLSIZEIPTR=1
fi

AC_CHECK_TYPES(GLintptr, [], [], [[$GL_INCLUDES]])
if test "x$ac_cv_type_GLintptr" = "xyes"; then
  GST_GL_HAVE_GLINTPTR=1
fi

CFLAGS=$old_CFLAGS

GL_CONFIG_DEFINES="$GL_CONFIG_DEFINES
#define GST_GL_HAVE_GLEGLIMAGEOES $GST_GL_HAVE_GLEGLIMAGEOES
#define GST_GL_HAVE_GLCHAR $GST_GL_HAVE_GLCHAR
#define GST_GL_HAVE_GLSIZEIPTR $GST_GL_HAVE_GLSIZEIPTR
#define GST_GL_HAVE_GLINTPTR $GST_GL_HAVE_GLINTPTR
"

AC_CONFIG_COMMANDS([gst-libs/gst/gl/gstglconfig.h], [
	outfile=gstglconfig.h-tmp
	cat > $outfile <<\_______EOF
/* gstglconfig.h
 *
 * This is a generated file.  Please modify `configure.ac'
 */

#ifndef __GST_GL_CONFIG_H__
#define __GST_GL_CONFIG_H__

#include <gst/gst.h>

G_BEGIN_DECLS

_______EOF

	cat >>$outfile <<_______EOF
$gl_config_defines
_______EOF

	cat >>$outfile <<_______EOF

G_END_DECLS

#endif  /* __GST_GL_CONFIG_H__ */
_______EOF


	if cmp -s $outfile gst-libs/gst/gl/gstglconfig.h; then
          AC_MSG_NOTICE([gst-libs/gst/gl/gstglconfig.h is unchanged])
	  rm -f $outfile
	else
	  mv $outfile gst-libs/gst/gl/gstglconfig.h
	fi
],[
gl_config_defines='$GL_CONFIG_DEFINES'
])

dnl libpng is optional
PKG_CHECK_MODULES(LIBPNG, libpng >= 1.0, HAVE_PNG=yes, HAVE_PNG=no)
if test "x$HAVE_PNG" = "xyes"; then
  AC_DEFINE(HAVE_PNG, [1] , [Use libpng])
fi
AC_SUBST(HAVE_PNG)
AC_SUBST(LIBPNG_LIBS)
AC_SUBST(LIBPNG_CFLAGS)
AM_CONDITIONAL(HAVE_PNG, test "x$HAVE_PNG" = "xyes")

dnl libjpeg is optional
AC_ARG_WITH(jpeg-mmx, [  --with-jpeg-mmx, path to MMX'ified JPEG library])
OLD_LIBS="$LIBS"
if test x$with_jpeg_mmx != x; then
  LIBS="$LIBS -L$with_jpeg_mmx"
fi
AC_CHECK_LIB(jpeg-mmx, jpeg_set_defaults, HAVE_JPEG="yes", HAVE_JPEG="no")
JPEG_LIBS="$LIBS -ljpeg-mmx"
LIBS="$OLD_LIBS"
if test x$HAVE_JPEG != xyes; then
  JPEG_LIBS="-ljpeg"
  AC_CHECK_LIB(jpeg, jpeg_set_defaults, HAVE_JPEG="yes", HAVE_JPEG="no")
fi

if test x$HAVE_JPEG = xyes; then
  AC_DEFINE(HAVE_JPEG, [1], [Use libjpeg])
else
  AC_DEFINE(HAVE_JPEG, [0], [Use libjpeg])
  JPEG_LIBS=
fi
AC_SUBST(JPEG_LIBS)
AC_SUBST(HAVE_JPEG)
AM_CONDITIONAL(HAVE_JPEG, test "x$HAVE_JPEG" = "xyes")


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
  LIBS="$LIBS -ldsound -ldxerr9 -luser32"
  AC_MSG_CHECKING(for DirectSound LDFLAGS)
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <windows.h>
#include <dxerr9.h>
#include <dsound.h>
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
    DIRECTSOUND_LIBS="-ldsound -ldxerr9 -luser32"
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

dnl DirectDraw
translit(dnm, m, l) AM_CONDITIONAL(USE_DIRECTDRAW, true)
AG_GST_CHECK_FEATURE(DIRECTDRAW, [DirectDraw plug-in], directdrawsink, [
  HAVE_DIRECTDRAW="no"
  save_CFLAGS="$CFLAGS"
  save_LDFLAGS="$LDFLAGS"
  save_LIBS="$LIBS"
  CFLAGS="$CFLAGS $DIRECTX_CFLAGS"
  LDFLAGS="$LDFLAGS $DIRECTX_LDFLAGS"
  LIBS="$LIBS -lddraw -lgdi32"
  AC_MSG_CHECKING(for DirectDraw LDFLAGS)
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
#include <windows.h>
#include <ddraw.h>
]], [[
  GetStockObject(0);
  DirectDrawCreate(NULL, NULL, NULL);
]])],
    [HAVE_DIRECTDRAW="yes"],
    [HAVE_DIRECTDRAW="no"])
  AC_MSG_RESULT($HAVE_DIRECTDRAW)
  CFLAGS=$save_CFLAGS
  LDFLAGS=$save_LDFLAGS
  LIBS=$save_LIBS

  if test "x$HAVE_DIRECTDRAW" = "xyes";  then
    dnl this is much more than we want
    DIRECTDRAW_LIBS="-lddraw -ldxguid -lgdi32"
    AC_SUBST(DIRECTX_CFLAGS)
    AC_SUBST(DIRECTX_LDFLAGS)
    AC_SUBST(DIRECTDRAW_LIBS)
  fi
  AC_SUBST(HAVE_DIRECTDRAW)
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
  PKG_CHECK_MODULES([BLUEZ], [bluez < 5.0], [
    dnl FIXME: we should be able to replace this with the GLib DBus stuff
    PKG_CHECK_MODULES([DBUS], [dbus-1], [HAVE_BLUEZ=yes], [HAVE_BLUEZ=no])
  ], [
    HAVE_BLUEZ=no
  ])
])

dnl *** OS X videosrc ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OSX_VIDEO, true)
HAVE_OSX_VIDEO="no"
AG_GST_CHECK_FEATURE(OSX_VIDEO, [OSX video], osxvideosrc, [
  AC_CHECK_TYPE([SeqGrabComponent], HAVE_OSX_VIDEO="yes", HAVE_OSX_VIDEO="no",
      [#include <Quicktime/Quicktime.h>])
])
dnl in case header Quicktime/Quicktime.h is found on other platforms
case "$host" in
  *-*darwin*)
    ;;
  *)
    HAVE_OSX_VIDEO="no"
    ;;
esac

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

dnl check for QuickTime
translit(dnm, m, l) AM_CONDITIONAL(USE_QUICKTIME, true)
AG_GST_CHECK_FEATURE(QUICKTIME, [QuickTime wrapper], qtwrapper, [
  AC_CHECK_TYPE([SampleReferenceRecord], HAVE_QUICKTIME="yes", HAVE_QUICKTIME="no",
      [#include <Quicktime/Quicktime.h>])
])

dnl check for shm_open (for shm plugin)
translit(dnm, m, l) AM_CONDITIONAL(USE_SHM, true)
AG_GST_CHECK_FEATURE(SHM, [POSIX shared memory source and sink], shm, [
    if test "x$HAVE_SYS_SOCKET_H" = "xyes"; then
        case $host in
        *-darwin* | *-macos10*)
            AC_DEFINE(HAVE_OSX,[1],[Apple Mac OS X operating system detected])
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
  AG_GST_PKG_CHECK_MODULES(GST_VIDEO, gstreamer-video-1.0)
  PKG_CHECK_MODULES(G_UDEV, gudev-1.0 , [
          AC_DEFINE([HAVE_GUDEV], 1, [Define if gudev is installed])
          HAVE_GUDEV="yes" ],
          [HAVE_GUDEV="no"])
  PKG_CHECK_MODULES(LIBUSB, libusb-1.0 , [
          AC_DEFINE([HAVE_LIBUSB], 1, [Define if libusb 1.x is installed])
          HAVE_LIBUSB="yes" ],
          [HAVE_LIBUSB="no"])
  if test "x$HAVE_UVCVIDEO_H" == "xyes" && \
     test "x$HAVE_GUDEV" == "xyes" && \
     test "x$HAVE_LIBUSB" == "xyes"; then
    HAVE_UVCH264=yes
  else
    HAVE_UVCH264=no
  fi
])
AC_SUBST(LIBUDEV_CFLAGS)
AC_SUBST(LIBUDEV_LIBS)
AC_SUBST(LIBUSB_CFLAGS)
AC_SUBST(LIBUSB_LIBS)


dnl *** ext plug-ins ***
dnl keep this list sorted alphabetically !

if test "x$BUILD_EXTERNAL" = "xyes"; then

dnl *** assrender ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ASSRENDER, true)
AG_GST_CHECK_FEATURE(ASSRENDER, [ASS/SSA renderer], assrender, [
  PKG_CHECK_MODULES(ASSRENDER, libass >= 0.9.4, [
    HAVE_ASSRENDER="yes" ], [
    HAVE_ASSRENDER="no"
  ])
])
AC_SUBST(ASSRENDER_CFLAGS)
AC_SUBST(ASSRENDER_LIBS)

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

dnl *** apexsink ***
translit(dnm, m, l) AM_CONDITIONAL(USE_APEXSINK, true)
AG_GST_CHECK_FEATURE(APEXSINK, [AirPort Express Wireless sink], apexsink, [
  PKG_CHECK_MODULES(APEXSINK, [ openssl >= 0.9.5  libcrypto ], [
    HAVE_APEXSINK="yes"
    saved_LIBS="$LIBS"
    LIBS=""
    AC_SEARCH_LIBS(socket, [socket], [ ], [ APEXSINK="no" ])
    AC_SEARCH_LIBS(gethostbyname, [nsl], [ ], [ APEXSINK="no" ])
    APEXSINK_LIBS="$APEXSINK_LIBS $LIBS"
    LIBS="$saved_LIBS"
    AC_SUBST(APEXSINK_CFLAGS)
    AC_SUBST(APEXSINK_LIBS)
  ], [
    APEXSINK="no"
  ])
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
  PKG_CHECK_MODULES(CURL, libcurl >= 7.21.0, [
    HAVE_CURL="yes"
    AC_CHECK_HEADERS([unistd.h sys/socket.h sys/types.h netinet/in.h netinet/ip.h netinet/tcp.h fcntl.h], [ ], [
      AC_CHECK_HEADERS([unistd.h sys/types.h winsock2.h ws2tcpip.h fcntl.h], [ ], [HAVE_CURL="no"])
    ])
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
  PKG_CHECK_MODULES(LIBXML2, libxml-2.0 >= 2.4, [HAVE_LIBXML2="yes"
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
     *)
       HAVE_DECKLINK=no
       ;;
   esac

   AC_SUBST(DECKLINK_CXXFLAGS)
   AC_SUBST(DECKLINK_LIBS)
])
AM_CONDITIONAL(DECKLINK_OSX, test "x$HAVE_DECKLINK_OSX" = xyes)

dnl **** DirectFB ****
translit(dnm, m, l) AM_CONDITIONAL(USE_DIRECTFB, true)
AG_GST_CHECK_FEATURE(DIRECTFB, [directfb], dfbvideosink , [
  PKG_CHECK_MODULES(DIRECTFB, directfb >= 0.9.24, HAVE_DIRECTFB="yes", [
    HAVE_DIRECTFB="no"
  ])
])

dnl **** Wayland ****
translit(dnm, m, l) AM_CONDITIONAL(USE_WAYLAND, true)
AG_GST_CHECK_FEATURE(WAYLAND, [wayland sink], wayland , [
  PKG_CHECK_MODULES(WAYLAND, wayland-client >= 1.4.0 wayland-scanner, [
    HAVE_WAYLAND="yes" ], [ HAVE_WAYLAND="no"
  ])
])

AC_PATH_PROG([wayland_scanner], [wayland-scanner])

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

dnl **** Free AAC Decoder (FAAD) ****
translit(dnm, m, l) AM_CONDITIONAL(USE_FAAD, true)
AG_GST_CHECK_FEATURE(FAAD, [AAC decoder plug-in], faad, [
  HAVE_FAAD="yes"
  faad_hdr=""
  AG_GST_CHECK_LIBHEADER(FAAD, faad, faacDecOpen, $LIBM, faad.h,
    FAAD_LIBS="-lfaad $LIBM", HAVE_FAAD="no")
  if test $HAVE_FAAD = "no"; then
    AG_GST_CHECK_LIBHEADER(FAAD, faad, NeAACDecOpen, $LIBM, neaacdec.h,
      [ FAAD_LIBS="-lfaad $LIBM"
        AC_DEFINE(FAAD_IS_NEAAC, 1, [Define if AAC is using new api prefix])
        faad_hdr="neaacdec.h"
      ], [
        HAVE_FAAD="no"
      ],)
  else
    faad_hdr="faad.h"
  fi
  if test $HAVE_FAAD = "yes"; then
    AC_MSG_CHECKING([Checking for FAAD >= 2.0])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <faad.h>]], [[
        #if !defined(FAAD2_VERSION) || !defined(FAAD_FMT_DOUBLE)
        #error Not faad2
        #else
        /* Release candidate of 2.0 is not good enough for us. This field
         * was added only in 2.0 final and does not exist in 2.0-RC3 */
        faacDecConfiguration conf; conf.dontUpSampleImplicitSBR = 1;
        #endif
    ]])],[
      HAVE_FAAD="yes"

      AC_MSG_CHECKING([Checking FAAD2 version in $faad_hdr])
      for minor in 10 9 8 7 6 5 0; do
        if test x$faad2_minor_version = "x"; then
          AC_EGREP_CPP([GST_CHECK_FAAD_VERSION \"2\.$minor\"], [
              #include <$faad_hdr>
              GST_CHECK_FAAD_VERSION FAAD2_VERSION
            ], [
              faad2_minor_version=$minor
            ])
        fi
      done
      if test x$faad2_minor_version = "x"; then
        AC_MSG_RESULT([no idea])
      else
        AC_MSG_RESULT([2.$faad2_minor_version])
        AC_DEFINE_UNQUOTED([FAAD2_MINOR_VERSION], [$faad2_minor_version], [The x in 2.x])
      fi
    ],[
      HAVE_FAAD="no"
    ])
  fi
  AC_SUBST(FAAD_LIBS)
  AC_SUBST(FAAD_IS_NEAAC)
])

dnl *** fbdev ***
translit(dnm, m, l) AM_CONDITIONAL(USE_FBDEV, true)
AG_GST_CHECK_FEATURE(FBDEV, [linux framebuffer], fbdevsink, [
  if test "x$HAVE_FBDEV" != "xyes"; then
    AC_CHECK_HEADER([linux/fb.h], HAVE_FBDEV=yes, HAVE_FBDEV=no)
  fi
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

dnl *** LV2 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LV2, true)
AG_GST_CHECK_FEATURE(LV2, [lv2], lv2, [
  PKG_CHECK_MODULES(SLV2, slv2 >= 0.6.6, HAVE_LV2="yes", HAVE_LV2="no")
  AC_SUBST(SLV2_CFLAGS)
  AC_SUBST(SLV2_LIBS)
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


dnl *** libsrtp ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SRTP, true)
AG_GST_CHECK_FEATURE(SRTP, [srtp library], srtp, [
  HAVE_SRTP="yes"
  AG_GST_CHECK_LIBHEADER(SRTP, srtp, srtp_init, , srtp/srtp.h,
    SRTP_LIBS="-lsrtp", HAVE_SRTP="no")
  AC_SUBST(SRTP_LIBS)
])

dnl *** linsys ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LINSYS, true)
AG_GST_CHECK_FEATURE(LINSYS, [Linear Systems SDI plugin], linsys, [
  case "$host" in
    *-*linux*)
      HAVE_LINSYS=yes
      ;;
    *)
      HAVE_LINSYS=no
      ;;
  esac
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

dnl *** mimic ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MIMIC, true)
AG_GST_CHECK_FEATURE(MIMIC, [libmimic library], mimic, [
  MIMIC_REQUIRED=1.0
  PKG_CHECK_MODULES(MIMIC, libmimic >= $MIMIC_REQUIRED,
      HAVE_MIMIC=yes, HAVE_MIMIC=no)
  AC_SUBST(MIMIC_CFLAGS)
  AC_SUBST(MIMIC_LIBS)
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
        LIBS="$LIBS $MJPEG_LIBS -lmjpegutils $LIBM -lpthread"
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
        MPEG2ENC_LIBS="$MPEG2ENC_LIBS -lmpeg2encpp $LIBM -lpthread"
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

dnl *** mpg123 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MPG123, true)
AG_GST_CHECK_FEATURE(MPG123, [mpg123 audio decoder], mpg123, [
  PKG_CHECK_MODULES(MPG123, libmpg123 >= 1.13, HAVE_MPG123="yes", HAVE_MPG123="no")
  AC_SUBST(MPG123_CFLAGS)
  AC_SUBST(MPG123_LIBS)
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

dnl *** MythTV ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MYTHTV, true)
AG_GST_CHECK_FEATURE(MYTHTV, [MythTV client plugins], mythtvsrc, [
  PKG_CHECK_MODULES(GMYTH, gmyth >= 0.4 gmyth <= 0.7.99, HAVE_MYTHTV="yes", [
    HAVE_MYTHTV="no"
  ])
  AC_SUBST(GMYTH_CFLAGS)
  AC_SUBST(GMYTH_LIBS)
])

dnl *** nas ***
translit(dnm, m, l) AM_CONDITIONAL(USE_NAS, true)
AG_GST_CHECK_FEATURE(NAS, [nas plug-in], nassink, [
  HAVE_NAS="no"
  AG_GST_CHECK_X
  if test "x$HAVE_X" = "xyes"; then
    save_cppflags=$CFLAGS
    CPPFLAGS="$CPPFLAGS $X_CFLAGS"
    AG_GST_CHECK_LIBHEADER(NAS, audio, AuOpenServer, $X_LIBS, audio/audiolib.h,
      NAS_LIBS="$X_LIBS -laudio" NAS_CFLAGS="$X_CFLAGS")
    CPPFLAGS="$save_cppflags"
  fi
  AC_SUBST(NAS_CFLAGS)
  AC_SUBST(NAS_LIBS)
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
  PKG_CHECK_MODULES(OPENCV, opencv >= 2.0.0 opencv < 2.5.0 , [
    AC_PROG_CXX
    AC_LANG([C++])
    OLD_CPPFLAGS=$CPPFLAGS
    CPPFLAGS=$OPENCV_CFLAGS
    AC_CHECK_HEADERS([highgui.h opencv2/highgui/highgui_c.h])
    CPPFLAGS=$OLD_CPPFLAGS
    AC_LANG([C])

    if test $ac_cv_header_highgui_h = "yes" -o $ac_cv_header_opencv2_highgui_highgui_c_h = "yes" ; then
      HAVE_OPENCV="yes"
    else
      AC_MSG_RESULT([neither highgui.h nor opencv2/highgui/highgui_c.h could not be found])
      HAVE_OPENCV="no"
    fi
  ], [
    HAVE_OPENCV="no"
  ])
  OPENCV_PREFIX="`$PKG_CONFIG --variable=prefix opencv`"
  AC_DEFINE_UNQUOTED(OPENCV_PREFIX, "$OPENCV_PREFIX", [opencv install prefix])
  AC_SUBST(OPENCV_PREFIX)
  AC_SUBST(OPENCV_CFLAGS)
  AC_SUBST(OPENCV_LIBS)
])

dnl *** OpenEXR ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OPENEXR, true)
AG_GST_CHECK_FEATURE(OPENEXR, [openexr library], openexr, [
  AG_GST_PKG_CHECK_MODULES(OPENEXR, OpenEXR)
])

dnl *** OpenJPEG ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OPENJPEG, true)
AG_GST_CHECK_FEATURE(OPENJPEG, [openjpeg library], openjpeg, [
  HAVE_OPENJPEG="no"

  save_LIBS="$LIBS"
  save_CFLAGS="$CFLAGS"
  OPENJPEG_LIBS="-lopenjp2"
  LIBS="$LIBS $OPENJPEG_LIBS"
  CFLAGS="$CFLAGS $OPENJPEG_CFLAGS"
  AC_LINK_IFELSE([
    AC_LANG_PROGRAM([#include <openjpeg-2.0/openjpeg.h>],
                    [return (int) *opj_version ();])
                 ], [HAVE_OPENJPEG=yes], [HAVE_OPENJPEG=no])
  LIBS="$save_LIBS"
  CFLAGS="$save_CFLAGS"

  if test x"$HAVE_OPENJPEG" = x"no"; then
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

dnl *** pvr ***
translit(dnm, m, l) AM_CONDITIONAL(USE_PVR, true)
AG_GST_CHECK_FEATURE(PVR, [pvrvideosink], pvr, [
  PKG_CHECK_MODULES([PVR], [libtimemmgr], HAVE_PVR=yes, HAVE_PVR=no)
  AC_SUBST(PVR_CFLAGS)
  AC_SUBST(PVR_LIBS)
])

 AC_ARG_WITH([pvr-external-headers],
              AS_HELP_STRING([--with-pvr-external-headers],[Use system installed PVR2D headers]),
              [AS_IF([test "x$with_pvr_external_headers" = "xno"],
                     [PVR_CFLAGS="$PVR_CFLAGS -I\$(srcdir)/pvr_includes"])],
              [PVR_CFLAGS="$PVR_CFLAGS -I\$(srcdir)/pvr_includes"])

dnl *** rsvg ***
translit(dnm, m, l) AM_CONDITIONAL(USE_RSVG, true)
AG_GST_CHECK_FEATURE(RSVG, [rsvg decoder], rsvg, [
  PKG_CHECK_MODULES(RSVG, librsvg-2.0 >= 2.36 cairo, [
    HAVE_RSVG="yes"
    PKG_CHECK_MODULES(RSVG_2_36_2, librsvg-2.0 >= 2.36.2, HAVE_RSVG_2_36_2="yes", HAVE_RSVG_2_36_2="no")
    if test "x$HAVE_RSVG_2_36_2" = "xyes"; then
      AC_DEFINE(HAVE_RSVG_2_36_2, [1], [Have RSVG 2.36.2 or newer])
    fi
  ], [
    HAVE_RSVG="no"
  ])
  AC_SUBST(RSVG_CFLAGS)
  AC_SUBST(RSVG_LIBS)
])

dnl *** gl ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GL, true)
AG_GST_CHECK_FEATURE(GL, [gl elements], gl, [
  HAVE_GL="no"

  if test x"$USE_OPENGL" = x"yes" -o x"$USE_GLES2" = x"yes"; then
    HAVE_GL="yes"
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

dnl *** timidity ***
translit(dnm, m, l) AM_CONDITIONAL(USE_TIMIDITY, true)
AG_GST_CHECK_FEATURE(TIMIDITY, [timidity midi soft synth plugin], timidity, [
  PKG_CHECK_MODULES(TIMIDITY, libtimidity, [
    HAVE_TIMIDITY="yes",
	AC_MSG_CHECKING([for timidity.cfg])
	timidity_cfg=""
	if test -r /etc/timidity.cfg; then
	  timidity_cfg=/etc/timidity.cfg
	elif test -r /etc/timidity/timidity.cfg; then
	  timidity_cfg=/etc/timidity/timidity.cfg
	elif test -r /usr/share/timidity/timidity.cfg; then
	  timidity_cfg=/usr/share/timidity/timidity.cfg
	elif test -r /usr/local/share/timidity/timidity.cfg; then
	  timidity_cfg=/usr/local/share/timidity/timidity.cfg
	fi
	if test "x$timidity_cfg" != "x"; then
	  AC_MSG_RESULT($timidity_cfg)
      AC_DEFINE_UNQUOTED(TIMIDITY_CFG, "$timidity_cfg", [Define location of timidity.cfg])
	else
	  AC_MSG_RESULT([not found])
	fi
  ], [
    HAVE_TIMIDITY="no"
  ])
  AC_SUBST(TIMIDITY_CFLAGS)
  AC_SUBST(TIMIDITY_LIBS)
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
  AC_CHECK_LIB(WildMidi, WildMidi_LoadSamples, AC_DEFINE(HAVE_WILDMIDI_0_2_2, [1], [Have WildMidi 0.2.2 or earlier library]))

  if test "x$HAVE_WILDMIDI" = "xyes"; then
	 WILDMIDI_CFLAGS=
	 WILDMIDI_LIBS=-lWildMidi
	 AC_MSG_NOTICE(compiling gstWildMidi)
  else
	 WILDMIDI_CFLAGS=
	 WILDMIDI_LIBS=
	 AC_MSG_WARN(no WildMidi library found (libWildMidi))
  fi
  AC_SUBST(WILDMIDI_CFLAGS)
  AC_SUBST(WILDMIDI_LIBS)
])

dnl *** SDL ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SDL, true)
AG_GST_CHECK_FEATURE(SDL, [SDL plug-in], sdlvideosink sdlaudiosink, [
    AM_PATH_SDL(, HAVE_SDL=yes, HAVE_SDL=no)
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

dnl *** XVID ***
translit(dnm, m, l) AM_CONDITIONAL(USE_XVID, true)
AG_GST_CHECK_FEATURE(XVID, [xvid plugins], xvid, [
  HAVE_XVID=no
  AC_CHECK_HEADER(xvid.h, [
    OLD_LIBS="$LIBS"
    LIBS="-lpthread $LIBM"
    AC_CHECK_LIB(xvidcore, xvid_encore, [
      AC_CHECK_LIB(xvidcore, xvid_decore, [
        AC_CHECK_LIB(xvidcore, xvid_global, [
          AC_MSG_CHECKING([for up-to-date XviD API version])
          AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <xvid.h>]], [[
              #if XVID_API_MAJOR(XVID_API) != 4
              #error "Incompatible XviD API version"
              #endif
	      #if XVID_API_MAJOR(XVID_API) == 4 && XVID_API_MINOR(XVID_API) < 3
              #error "Incompatible XviD API version"
	      #endif
          ]])],[ AC_MSG_RESULT(yes)
            XVID_LIBS="-lxvidcore $LIBM"
            AC_SUBST(XVID_LIBS)
            HAVE_XVID=yes
          ],[AC_MSG_RESULT(no) ])
        ], )
      ], )
    ], )
    LIBS="$OLD_LIBS"
  ], )
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

dnl *** wininet ***
translit(dnm, m, l) AM_CONDITIONAL(USE_WININET, true)
AG_GST_CHECK_FEATURE(WININET, [Windows internet library], wininet, [
  AC_MSG_CHECKING([Checking for windows internet support])
  AC_CHECK_HEADERS([windows.h wininet.h],
      [HAVE_WININET="yes"], [HAVE_WININET="no"],
      [AC_INCLUDES_DEFAULT
#include <windows.h>])
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

dnl *** schroedinger ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SCHRO, true)
AG_GST_CHECK_FEATURE(SCHRO, [Schroedinger video codec], schro, [
  AG_GST_PKG_CHECK_MODULES(SCHRO, schroedinger-1.0 >= 1.0.10)
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

dnl *** GSettings ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GSETTINGS, true)
AG_GST_CHECK_FEATURE(GSETTINGS, [GSettings plugin], gsettings, [
  AG_GST_PKG_CHECK_MODULES(GSETTINGS, gio-2.0 >= 2.25.0)
  if test "x$HAVE_GSETTINGS" = "xyes"; then
    GLIB_GSETTINGS
  fi
])

dnl *** sndio ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SNDIO, true)
AG_GST_CHECK_FEATURE(SNDIO, [sndio audio], sndio, [
  AC_CHECK_HEADER(sndio.h, HAVE_SNDIO="yes", HAVE_SNDIO="no")
  if test "x$HAVE_SNDIO" = "xyes"; then
    AC_CHECK_LIB(sndio, sio_open, HAVE_SNDIO="yes", HAVE_SNDIO="no")
    if test "x$HAVE_SNDIO" = "xyes"; then
      SNDIO_LIBS=-lsndio
      AC_SUBST(SNDIO_LIBS)
    fi
  fi
])

dnl *** hls ***
translit(dnm, m, l) AM_CONDITIONAL(USE_HLS, true)
AG_GST_CHECK_FEATURE(HLS, [http live streaming plugin], hls, [
  PKG_CHECK_MODULES(NETTLE, nettle,
  [
    AC_DEFINE(HAVE_NETTLE, 1, [Define if nettle is available])
    HAVE_HLS="yes"
  ], [
    AM_PATH_LIBGCRYPT([1.2.0],
    [
      AC_DEFINE(HAVE_LIBGCRYPT, 1, [Define if libgcrypt is available])
      HAVE_HLS="yes"
    ], [
      HAVE_HLS="no"
    ])
  ])
])

else

dnl not building plugins with external dependencies,
dnl but we still need to set the conditionals

AM_CONDITIONAL(USE_ASSRENDER, false)
AM_CONDITIONAL(USE_VOAMRWBENC, false)
AM_CONDITIONAL(USE_VOAACENC, false)
AM_CONDITIONAL(USE_APEXSINK, false)
AM_CONDITIONAL(USE_BZ2, false)
AM_CONDITIONAL(USE_CHROMAPRINT, false)
AM_CONDITIONAL(USE_CURL, false)
AM_CONDITIONAL(USE_SSH2, false)
AM_CONDITIONAL(USE_DASH, false)
AM_CONDITIONAL(USE_DC1394, false)
AM_CONDITIONAL(USE_DECKLINK, false)
AM_CONDITIONAL(DECKLINK_OSX, false)
AM_CONDITIONAL(USE_DIRECTFB, false)
AM_CONDITIONAL(USE_WAYLAND, false)
AM_CONDITIONAL(USE_DAALA, false)
AM_CONDITIONAL(USE_DTS, false)
AM_CONDITIONAL(USE_EXIF, false)
AM_CONDITIONAL(USE_RESINDVD, false)
AM_CONDITIONAL(USE_FAAC, false)
AM_CONDITIONAL(USE_FAAD, false)
AM_CONDITIONAL(USE_FBDEV, false)
AM_CONDITIONAL(USE_FLITE, false)
AM_CONDITIONAL(USE_FLUIDSYNTH, false)
AM_CONDITIONAL(USE_GL, false)
AM_CONDITIONAL(USE_GSM, false)
AM_CONDITIONAL(USE_HLS, false)
AM_CONDITIONAL(USE_KATE, false)
AM_CONDITIONAL(USE_TIGER, false)
AM_CONDITIONAL(USE_LADSPA, false)
AM_CONDITIONAL(USE_LV2, false)
AM_CONDITIONAL(USE_LIBMMS, false)
AM_CONDITIONAL(USE_LINSYS, false)
AM_CONDITIONAL(USE_MODPLUG, false)
AM_CONDITIONAL(USE_MIMIC, false)
AM_CONDITIONAL(USE_MPEG2ENC, false)
AM_CONDITIONAL(USE_MPG123, false)
AM_CONDITIONAL(USE_MPLEX, false)
AM_CONDITIONAL(USE_MUSEPACK, false)
AM_CONDITIONAL(USE_MYTHTV, false)
AM_CONDITIONAL(USE_NAS, false)
AM_CONDITIONAL(USE_NEON, false)
AM_CONDITIONAL(USE_OFA, false)
AM_CONDITIONAL(USE_OPENAL, false)
AM_CONDITIONAL(USE_OPENCV, false)
AM_CONDITIONAL(USE_OPENEXR, false)
AM_CONDITIONAL(USE_OPENJPEG, false)
AM_CONDITIONAL(USE_OPENNI2, false)
AM_CONDITIONAL(USE_OPUS, false)
AM_CONDITIONAL(USE_PVR, false)
AM_CONDITIONAL(USE_LIBVISUAL, false)
AM_CONDITIONAL(USE_TIMIDITY, false)
AM_CONDITIONAL(USE_WILDMIDI, false)
AM_CONDITIONAL(USE_SDL, false)
AM_CONDITIONAL(USE_SMOOTHSTREAMING, false)
AM_CONDITIONAL(USE_SNDFILE, false)
AM_CONDITIONAL(USE_SOUNDTOUCH, false)
AM_CONDITIONAL(USE_SPANDSP, false)
AM_CONDITIONAL(USE_SPC, false)
AM_CONDITIONAL(USE_SRTP, false)
AM_CONDITIONAL(USE_GME, false)
AM_CONDITIONAL(USE_GSETTINGS, false)
AM_CONDITIONAL(USE_XVID, false)
AM_CONDITIONAL(USE_DVB, false)
AM_CONDITIONAL(USE_WININET, false)
AM_CONDITIONAL(USE_ACM, false)
AM_CONDITIONAL(USE_VDPAU, false)
AM_CONDITIONAL(USE_SBC, false)
AM_CONDITIONAL(USE_SCHRO, false)
AM_CONDITIONAL(USE_ZBAR, false)
AM_CONDITIONAL(USE_RSVG, false)
AM_CONDITIONAL(USE_RTMP, false)
AM_CONDITIONAL(USE_TELETEXTDEC, false)
AM_CONDITIONAL(USE_SNDIO, false)
AM_CONDITIONAL(USE_UVCH264, false)
AM_CONDITIONAL(USE_WEBP, false)

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
GST_CFLAGS="$GST_CFLAGS -DGST_USE_UNSTABLE_API"
GST_CXXFLAGS="$GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CXXFLAGS)"
GST_OBJCFLAGS="$GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_OBJCFLAGS)"
GST_CFLAGS="$GST_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CFLAGS)"
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

dnl po/Makefile.in

AC_CONFIG_FILES(
Makefile
common/Makefile
common/m4/Makefile
gst-plugins-bad.spec
gst/Makefile
gst/accurip/Makefile
gst/adpcmdec/Makefile
gst/adpcmenc/Makefile
gst/aiff/Makefile
gst/asfmux/Makefile
gst/audiofxbad/Makefile
gst/audiomixer/Makefile
gst/audiovisualizers/Makefile
gst/autoconvert/Makefile
gst/bayer/Makefile
gst/camerabin2/Makefile
gst/cdxaparse/Makefile
gst/coloreffects/Makefile
gst/dataurisrc/Makefile
gst/dccp/Makefile
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
gst/hdvparse/Makefile
gst/id3tag/Makefile
gst/inter/Makefile
gst/interlace/Makefile
gst/ivfparse/Makefile
gst/ivtc/Makefile
gst/jp2kdecimator/Makefile
gst/jpegformat/Makefile
gst/librfb/Makefile
gst/liveadder/Makefile
gst/midi/Makefile
gst/mpegdemux/Makefile
gst/mpegtsdemux/Makefile
gst/mpegtsmux/Makefile
gst/mpegtsmux/tsmux/Makefile
gst/mpegpsmux/Makefile
gst/mve/Makefile
gst/mxf/Makefile
gst/nuvdemux/Makefile
gst/patchdetect/Makefile
gst/pcapparse/Makefile
gst/pnm/Makefile
gst/rawparse/Makefile
gst/real/Makefile
gst/removesilence/Makefile
gst/sdi/Makefile
gst/sdp/Makefile
gst/segmentclip/Makefile
gst/siren/Makefile
gst/smooth/Makefile
gst/speed/Makefile
gst/subenc/Makefile
gst/stereo/Makefile
gst/tta/Makefile
gst/videofilters/Makefile
gst/videomeasure/Makefile
gst/videoparsers/Makefile
gst/videosignal/Makefile
gst/vmnc/Makefile
gst/y4m/Makefile
gst/yadif/Makefile
gst/compositor/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/basecamerabinsrc/Makefile
gst-libs/gst/gl/Makefile
gst-libs/gst/gl/android/Makefile
gst-libs/gst/gl/cocoa/Makefile
gst-libs/gst/gl/dispmanx/Makefile
gst-libs/gst/gl/glprototypes/Makefile
gst-libs/gst/gl/eagl/Makefile
gst-libs/gst/gl/egl/Makefile
gst-libs/gst/gl/wayland/Makefile
gst-libs/gst/gl/win32/Makefile
gst-libs/gst/gl/x11/Makefile
gst-libs/gst/insertbin/Makefile
gst-libs/gst/interfaces/Makefile
gst-libs/gst/codecparsers/Makefile
gst-libs/gst/mpegts/Makefile
gst-libs/gst/uridownloader/Makefile
gst-libs/gst/wayland/Makefile
gst-libs/gst/base/Makefile
gst-libs/gst/video/Makefile
sys/Makefile
sys/dshowdecwrapper/Makefile
sys/acmenc/Makefile
sys/acmmp3dec/Makefile
sys/androidmedia/Makefile
sys/applemedia/Makefile
sys/applemedia-nonpublic/Makefile
sys/avc/Makefile
sys/bluez/Makefile
sys/d3dvideosink/Makefile
sys/decklink/Makefile
sys/directdraw/Makefile
sys/directsound/Makefile
sys/dshowsrcwrapper/Makefile
sys/dshowvideosink/Makefile
sys/dvb/Makefile
sys/fbdev/Makefile
sys/linsys/Makefile
sys/opensles/Makefile
sys/osxvideo/Makefile
sys/qtwrapper/Makefile
sys/shm/Makefile
sys/uvch264/Makefile
sys/vcd/Makefile
sys/vdpau/Makefile
sys/pvr2d/Makefile
sys/wasapi/Makefile
sys/wininet/Makefile
sys/winks/Makefile
sys/winscreencap/Makefile
tests/Makefile
tests/check/Makefile
tests/files/Makefile
tests/examples/Makefile
tests/examples/camerabin2/Makefile
tests/examples/directfb/Makefile
tests/examples/gl/Makefile
tests/examples/gl/cocoa/Makefile
tests/examples/gl/clutter/Makefile
tests/examples/gl/generic/Makefile
tests/examples/gl/generic/cube/Makefile
tests/examples/gl/generic/cubeyuv/Makefile
tests/examples/gl/generic/doublecube/Makefile
tests/examples/gl/generic/recordgraphic/Makefile
tests/examples/gl/gtk/Makefile
tests/examples/gl/gtk/gtkvideooverlay/Makefile
tests/examples/gl/gtk/filternovideooverlay/Makefile
tests/examples/gl/gtk/filtervideooverlay/Makefile
tests/examples/gl/gtk/fxtest/Makefile
tests/examples/gl/gtk/switchvideooverlay/Makefile
tests/examples/gl/qt/Makefile
tests/examples/gl/sdl/Makefile
tests/examples/mpegts/Makefile
tests/examples/mxf/Makefile
tests/examples/opencv/Makefile
tests/examples/uvch264/Makefile
tests/icles/Makefile
ext/voamrwbenc/Makefile
ext/voaacenc/Makefile
ext/assrender/Makefile
ext/apexsink/Makefile
ext/bz2/Makefile
ext/chromaprint/Makefile
ext/curl/Makefile
ext/dash/Makefile
ext/dc1394/Makefile
ext/directfb/Makefile
ext/wayland/Makefile
ext/daala/Makefile
ext/dts/Makefile
ext/gl/Makefile
ext/faac/Makefile
ext/faad/Makefile
ext/flite/Makefile
ext/fluidsynth/Makefile
ext/gsm/Makefile
ext/hls/Makefile
ext/kate/Makefile
ext/ladspa/Makefile
ext/lv2/Makefile
ext/libmms/Makefile
ext/libvisual/Makefile
ext/Makefile
ext/modplug/Makefile
ext/mpeg2enc/Makefile
ext/mpg123/Makefile
ext/mimic/Makefile
ext/mplex/Makefile
ext/musepack/Makefile
ext/mythtv/Makefile
ext/nas/Makefile
ext/neon/Makefile
ext/ofa/Makefile
ext/openal/Makefile
ext/opencv/Makefile
ext/openexr/Makefile
ext/openjpeg/Makefile
ext/openni2/Makefile
ext/opus/Makefile
ext/rsvg/Makefile
ext/resindvd/Makefile
ext/rtmp/Makefile
ext/sbc/Makefile
ext/schroedinger/Makefile
ext/sdl/Makefile
ext/smoothstreaming/Makefile
ext/sndfile/Makefile
ext/soundtouch/Makefile
ext/spandsp/Makefile
ext/sndio/Makefile
ext/srtp/Makefile
ext/teletextdec/Makefile
ext/gme/Makefile
ext/gsettings/Makefile
ext/gsettings/org.freedesktop.gstreamer.default-elements.gschema.xml
ext/spc/Makefile
ext/timidity/Makefile
ext/webp/Makefile
ext/xvid/Makefile
ext/zbar/Makefile
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
pkgconfig/gstreamer-gl.pc
pkgconfig/gstreamer-gl-uninstalled.pc
pkgconfig/gstreamer-mpegts.pc
pkgconfig/gstreamer-mpegts-uninstalled.pc
pkgconfig/gstreamer-wayland.pc
pkgconfig/gstreamer-wayland-uninstalled.pc
pkgconfig/gstreamer-bad-base.pc
pkgconfig/gstreamer-bad-base-uninstalled.pc
pkgconfig/gstreamer-bad-video.pc
pkgconfig/gstreamer-bad-video-uninstalled.pc
tools/Makefile
m4/Makefile
)

dnl Create the config.h file for Visual Studio builds
dnl Beware of spaces and /'s in some of the shell variable contents.
sed \
    -e 's/.*config.h.in.*autoheader.*/\/* Autogenerated config.h created for win32 Visual Studio builds *\/\n\n\/* PREFIX -- specifically added for Windows for easier moving *\/\n#define PREFIX "C:\\\\gstreamer"/' \
    -e 's/.* GETTEXT_PACKAGE$/#define GETTEXT_PACKAGE "'$GETTEXT_PACKAGE'"/' \
    -e 's/.* GST_DATADIR$/#define GST_DATADIR PREFIX "\\\\share"/' \
    -e 's/.* GST_LEVEL_DEFAULT$/#define GST_LEVEL_DEFAULT GST_LEVEL_ERROR/' \
    -e 's/.* GST_LICENSE$/#define GST_LICENSE "'$GST_LICENSE'"/' \
    -e 's/.* GST_API_VERSION$/#define GST_API_VERSION "'$GST_API_VERSION'"/' \
    -e "s,.* GST_PACKAGE_NAME$,#define GST_PACKAGE_NAME \"${GST_PACKAGE_NAME}\"," \
    -e 's/.* GST_PACKAGE_ORIGIN$/#define GST_PACKAGE_ORIGIN "Unknown package origin"/' \
    -e "s,.* GST_PACKAGE_RELEASE_DATETIME$,#define GST_PACKAGE_RELEASE_DATETIME \"${GST_PACKAGE_RELEASE_DATETIME}\"," \
    -e 's/.* HAVE_CPU_I386$/#define HAVE_CPU_I386 1/' \
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
    -e 's/.* VERSION$/#define VERSION "'$VERSION'"/' \
    -e "s/.* DEFAULT_AUDIOSINK$/#define DEFAULT_AUDIOSINK \"directsoundsink\"/" \
    -e "s/.* DEFAULT_AUDIOSRC$/#define DEFAULT_AUDIOSRC \"audiotestsrc\"/" \
    -e "s/.* DEFAULT_VIDEOSRC$/#define DEFAULT_VIDEOSRC \"videotestsrc\"/" \
    -e "s/.* DEFAULT_VISUALIZER$/#define DEFAULT_VISUALIZER \"goom\"/" \
    -e 's/.* HAVE_WINDOWS_H$/#define HAVE_WINDOWS_H 1/' \
    -e 's/.* HAVE_WININET_H$/#define HAVE_WININET_H 1/' \
    -e 's/.* HAVE_WINSOCK2_H$/#define HAVE_WINSOCK2_H 1/' \
    -e 's/.* HAVE_WS2TCPIP_H$/#define HAVE_WS2TCPIP_H 1/' \
    config.h.in >win32/common/config.h-new

AC_OUTPUT

AG_GST_OUTPUT_PLUGINS
ORC_OUTPUT

