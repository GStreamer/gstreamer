AC_PREREQ(2.52)

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT(GStreamer Bad Plug-ins, 0.10.17.1,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gst-plugins-bad)

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability])

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_CVS="no", GST_CVS="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([docs/plugins/gst-plugins-bad-plugins-docs.sgml])

dnl define the output header for config
AM_CONFIG_HEADER([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl sets host_* variables
AC_CANONICAL_HOST

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$GST_PLUGINS_BAD_VERSION_MAJOR.$GST_PLUGINS_BAD_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)

dnl FIXME: this macro doesn't actually work;
dnl the generated libtool script has no support for the listed tags.
dnl So this needs to be fixed first if we want to use this
dnl AS_LIBTOOL_TAGS([CXX])

AC_LIBTOOL_WIN32_DLL
AM_PROG_LIBTOOL

dnl *** required versions of GStreamer stuff ***
GST_REQ=0.10.25.1
GSTPB_REQ=0.10.25.1

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl set up gettext
dnl the version check needs to stay here because autopoint greps for it
AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])
AG_GST_GETTEXT([gst-plugins-bad-$GST_MAJORMINOR])

dnl *** check for arguments to configure ***

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

dnl determine c++ compiler
AC_PROG_CXX
dnl determine if c++ is available on this system
AC_CHECK_PROG(HAVE_CXX, $CXX, yes, no)

AS_PROG_OBJC

dnl check if the compiler supports '-c' and '-o' options
AM_PROG_CC_C_O

dnl check if the compiler supports do while(0) macros
AG_GST_CHECK_DOWHILE_MACROS

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for documentation tools
GTK_DOC_CHECK([1.3])
AS_PATH_PYTHON([2.1])
AG_GST_PLUGIN_DOCS([1.3],[2.1])

dnl *** checks for libraries ***

dnl check for libm, for sin() etc.
AC_CHECK_LIBM
AC_SUBST(LIBM)

AC_FUNC_MMAP

dnl *** checks for header files ***

AC_CHECK_HEADERS([unistd.h], HAVE_UNISTD_H=yes)
AM_CONDITIONAL(HAVE_UNISTD_H, test "x$HAVE_UNISTD_H" = "xyes")

if test "x$HAVE_UNISTD_H" != "xyes"; then
  GST_PLUGINS_SELECTED=`echo $GST_PLUGINS_SELECTED | $SED -e s/festival//`
fi

dnl used by camerabin
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
AG_GST_GLIB_CHECK([2.16])

dnl liboil is required
PKG_CHECK_MODULES(LIBOIL, liboil-0.3 >= 0.3.8, HAVE_LIBOIL=yes, HAVE_LIBOIL=no)
if test "x$HAVE_LIBOIL" != "xyes"; then
  AC_ERROR([liboil-0.3.8 or later is required])
fi

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_MAJORMINOR, [$GST_REQ], yes)
AG_GST_CHECK_GST_BASE($GST_MAJORMINOR, [$GST_REQ], yes)
AG_GST_CHECK_GST_GDP($GST_MAJORMINOR, [$GST_REQ], yes)
AG_GST_CHECK_GST_CHECK($GST_MAJORMINOR, [$GST_REQ], no)
AG_GST_CHECK_GST_CONTROLLER($GST_MAJORMINOR, [$GST_REQ], yes)
AG_GST_CHECK_GST_PLUGINS_BASE($GST_MAJORMINOR, [$GSTPB_REQ], yes)
AM_CONDITIONAL(HAVE_GST_CHECK, test "x$HAVE_GST_CHECK" = "xyes")

GSTPB_PLUGINS_DIR=`$PKG_CONFIG gstreamer-plugins-base-$GST_MAJORMINOR --variable pluginsdir`
AC_SUBST(GSTPB_PLUGINS_DIR)
AC_MSG_NOTICE(Using GStreamer Base Plugins in $GSTPB_PLUGINS_DIR)

dnl Check for documentation xrefs
GLIB_PREFIX="`$PKG_CONFIG --variable=prefix glib-2.0`"
GST_PREFIX="`$PKG_CONFIG --variable=prefix gstreamer-$GST_MAJORMINOR`"
GSTPB_PREFIX="`$PKG_CONFIG --variable=prefix gstreamer-plugins-base-$GST_MAJORMINOR`"
AC_SUBST(GLIB_PREFIX)
AC_SUBST(GST_PREFIX)
AC_SUBST(GSTPB_PREFIX)

dnl GTK is optional and used in examples
HAVE_GTK=NO
PKG_CHECK_MODULES(GTK, gtk+-x11-2.0 >= 2.12.0, HAVE_GTK=yes, HAVE_GTK=no)
AM_CONDITIONAL(HAVE_GTK, test "x$HAVE_GTK" = "xyes")

dnl Needed for GtkBuilder to autoconnect signals
PKG_CHECK_MODULES(GMODULE_EXPORT, gmodule-export-2.0, HAVE_GMODULE_EXPORT=yes, HAVE_GMODULE_EXPORT=no)

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

dnl set license and copyright notice
GST_LICENSE="LGPL"
AC_DEFINE_UNQUOTED(GST_LICENSE, "$GST_LICENSE", [GStreamer license])
AC_SUBST(GST_LICENSE)

dnl define LIBDIR so we can inform people where we live
AS_AC_EXPAND(LIBDIR, $libdir)
AC_DEFINE_UNQUOTED(LIBDIR, "$LIBDIR", [library dir])

dnl set location of plugin directory
AG_GST_SET_PLUGINDIR

dnl define an ERROR_CFLAGS Makefile variable
AG_GST_SET_ERROR_CFLAGS($GST_CVS)

dnl define an ERROR_CXXFLAGS Makefile variable
AG_GST_SET_ERROR_CXXFLAGS($GST_CVS)

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT($GST_CVS)

dnl used in examples
AG_GST_DEFAULT_ELEMENTS

dnl *** plug-ins to include ***

dnl these are all the gst plug-ins, compilable without additional libs
AG_GST_CHECK_PLUGIN(adpcmdec)
AG_GST_CHECK_PLUGIN(adpcmenc)
AG_GST_CHECK_PLUGIN(aiff)
AG_GST_CHECK_PLUGIN(asfmux)
AG_GST_CHECK_PLUGIN(audioparsers)
AG_GST_CHECK_PLUGIN(autoconvert)
AG_GST_CHECK_PLUGIN(camerabin)
AG_GST_CHECK_PLUGIN(legacyresample)
AG_GST_CHECK_PLUGIN(bayer)
AG_GST_CHECK_PLUGIN(cdxaparse)
AG_GST_CHECK_PLUGIN(dccp)
AG_GST_CHECK_PLUGIN(debugutils)
AG_GST_CHECK_PLUGIN(dtmf)
AG_GST_CHECK_PLUGIN(dvdspu)
AG_GST_CHECK_PLUGIN(festival)
AG_GST_CHECK_PLUGIN(freeze)
AG_GST_CHECK_PLUGIN(frei0r)
AG_GST_CHECK_PLUGIN(h264parse)
AG_GST_CHECK_PLUGIN(hdvparse)
AG_GST_CHECK_PLUGIN(id3tag)
AG_GST_CHECK_PLUGIN(librfb)
AG_GST_CHECK_PLUGIN(liveadder)
AG_GST_CHECK_PLUGIN(mpegdemux)
AG_GST_CHECK_PLUGIN(mpegtsmux)
AG_GST_CHECK_PLUGIN(mpegpsmux)
AG_GST_CHECK_PLUGIN(mpeg4videoparse)
AG_GST_CHECK_PLUGIN(mpegvideoparse)
AG_GST_CHECK_PLUGIN(mve)
AG_GST_CHECK_PLUGIN(mxf)
AG_GST_CHECK_PLUGIN(nsf)
AG_GST_CHECK_PLUGIN(nuvdemux)
AG_GST_CHECK_PLUGIN(pcapparse)
AG_GST_CHECK_PLUGIN(pnm)
AG_GST_CHECK_PLUGIN(qtmux)
AG_GST_CHECK_PLUGIN(rawparse)
AG_GST_CHECK_PLUGIN(real)
AG_GST_CHECK_PLUGIN(rtpmux)
AG_GST_CHECK_PLUGIN(scaletempo)
AG_GST_CHECK_PLUGIN(sdp)
AG_GST_CHECK_PLUGIN(selector)
AG_GST_CHECK_PLUGIN(shapewipe)
AG_GST_CHECK_PLUGIN(siren)
AG_GST_CHECK_PLUGIN(speed)
AG_GST_CHECK_PLUGIN(subenc)
AG_GST_CHECK_PLUGIN(stereo)
AG_GST_CHECK_PLUGIN(tta)
AG_GST_CHECK_PLUGIN(valve)
AG_GST_CHECK_PLUGIN(videomeasure)
AG_GST_CHECK_PLUGIN(videosignal)
AG_GST_CHECK_PLUGIN(vmnc)

dnl *** plug-ins to exclude ***

dnl real plugin only works on i386 and x86_64 for the time being.
if test "x$HAVE_CPU_I386" != "xyes" && test "x$HAVE_CPU_X86_64" != "xyes"; then
  AC_MSG_WARN([Not building real plugin, only works on 32bit and 64bit x86 platforms])
  AG_GST_DISABLE_PLUGIN(real)
fi

dnl disable experimental plug-ins
dnl if test "x$BUILD_EXPERIMENTAL" != "xyes"; then
dnl fi

# This will always succeed because we depend on GLib >= 2.16
PKG_CHECK_MODULES(GIO, gio-2.0 >= 2.16, HAVE_GIO=yes, HAVE_GIO=no)
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

dnl disable gst plugins we might not be able to build on this
dnl platform: librfb (ugly but minimally invasive)
dnl FIXME: maybe move to sys, or make work with winsock2
AC_CHECK_HEADERS([sys/socket.h], HAVE_SYS_SOCKET_H=yes)
AC_CHECK_HEADERS([winsock2.h], HAVE_WINSOCK2_H=yes)

if test "x$HAVE_WINSOCK2_H" = "xyes"; then
  WIN32_LIBS="-lws2_32"
  AC_SUBST(WIN32_LIBS)
fi

dnl needed for festival
AM_CONDITIONAL(HAVE_WINSOCK2_H, test "x$HAVE_WINSOCK2_H" = "xyes")

if test "x$HAVE_WINSOCK2_H" = "xyes"; then
  DCCP_LIBS="$DCCP_LIBS -lws2_32"
  XDG_LIBS="$XDG_LIBS -lws2_32"
  AC_SUBST(DCCP_LIBS)
  AC_SUBST(XDG_LIBS)
fi

if test "x$HAVE_PTHREAD_H" = "xyes"; then
  DCCP_LIBS="$DCCP_LIBS -lpthread"
  AC_SUBST(DCCP_LIBS)
fi

if test "x$HAVE_PTHREAD_H" != "xyes"; then
  AG_GST_DISABLE_PLUGIN(dccp)
fi

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
  AC_LINK_IFELSE([
#include <windows.h>
#include <dxerr9.h>
#include <dsound.h>

int main ()
{
  DXGetErrorString9 (0);
  DirectSoundCreate(NULL, NULL, NULL);

  return 0;
}
],
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
  AC_LINK_IFELSE([
#include <windows.h>
#include <ddraw.h>

int main ()
{
  GetStockObject(0);
  DirectDrawCreate(NULL, NULL, NULL);

  return 0;
}
],
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

dnl *** OS X videosrc ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OSX_VIDEO, true)
HAVE_OSX_VIDEO="no"
AG_GST_CHECK_FEATURE(OSX_VIDEO, [OSX video], osxvideosrc, [
  AC_CHECK_HEADER(Quicktime/Quicktime.h, HAVE_OSX_VIDEO="yes", HAVE_OSX_VIDEO="no")
])
dnl in case header Quicktime/Quicktime.h is found on other platforms
case "$host" in
  *-*darwin*)
    dnl do nothing
    ;;
  *)
    HAVE_OSX_VIDEO="no"
    ;;
esac

dnl check for QuickTime
translit(dnm, m, l) AM_CONDITIONAL(USE_QUICKTIME, true)
AG_GST_CHECK_FEATURE(QUICKTIME, [QuickTime wrapper], qtwrapper, [
  AC_CHECK_HEADER(QuickTime/Movies.h, HAVE_QUICKTIME="yes", HAVE_QUICKTIME="no")
])

dnl check for Video CD
translit(dnm, m, l) AM_CONDITIONAL(USE_VCD, true)
AG_GST_CHECK_FEATURE(VCD, [Video CD], vcdsrc, [
  AC_CHECK_HEADER(linux/cdrom.h, HAVE_VCD="yes", HAVE_VCD="no")
])

dnl *** ext plug-ins ***
dnl keep this list sorted alphabetically !

if test "x$BUILD_EXTERNAL" = "xyes"; then

dnl *** alsa ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ALSA, true)
AG_GST_CHECK_FEATURE(ALSA, [alsa plug-ins], gstalsa, [
  PKG_CHECK_MODULES(ALSA, alsa >= 0.9.1, [
    HAVE_ALSA="yes"
    AC_SUBST(ALSA_CFLAGS)
    AC_SUBST(ALSA_LIBS)
  ], [
    AM_PATH_ALSA(0.9.1, HAVE_ALSA="yes", HAVE_ALSA="no")
  ])
])

dnl *** assrender ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ASSRENDER, true)
AG_GST_CHECK_FEATURE(ASSRENDER, [ASS/SSA renderer], assrender, [
  PKG_CHECK_MODULES(ASSRENDER, libass >= 0.9.4, [
    HAVE_ASSRENDER="yes" ], [
    HAVE_ASSRENDER="no"
    AC_MSG_RESULT(no)
  ])
])
AC_SUBST(ASSRENDER_CFLAGS)
AC_SUBST(ASSRENDER_LIBS)

dnl *** amrwb ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AMRWB, true)
AG_GST_CHECK_FEATURE(AMRWB, [amrwb library], amrwbenc, [
  AG_GST_CHECK_LIBHEADER(AMRWB, amrwb,
                        GP3E_IF_encode, ,
                        amrwb/enc.h,
                        AMRWB_LIBS="-lamrwb"
                        AC_SUBST(AMRWB_LIBS))
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
  AG_GST_CHECK_LIBHEADER(BZ2, bz2, BZ2_bzCompress, ,bzlib.h, BZ2_LIBS="-lbz2")
  AC_SUBST(BZ2_LIBS)
])

#dnl *** cairo ***
#translit(dnm, m, l) AM_CONDITIONAL(USE_CAIRO, true)
#AG_GST_CHECK_FEATURE(CAIRO, [cairo plug-in], cairo, [
#  PKG_CHECK_MODULES(CAIRO, cairo >= 1.0 glitz-glx, HAVE_CAIRO=yes, [
#    HAVE_CAIRO=no
#    AC_MSG_RESULT(no)
#  ])
#  AC_SUBST(CAIRO_CFLAGS)
#  AC_SUBST(CAIRO_LIBS)
#])

dnl *** cdaudio ***
translit(dnm, m, l) AM_CONDITIONAL(USE_CDAUDIO, true)
AG_GST_CHECK_FEATURE(CDAUDIO, [cdaudio], cdaudio, [
  AG_GST_CHECK_CONFIGPROG(CDAUDIO, libcdaudio-config)
  AC_SUBST(CDAUDIO_CFLAGS)
  AC_SUBST(CDAUDIO_LIBS)
])

dnl *** celt ***
translit(dnm, m, l) AM_CONDITIONAL(USE_CELT, true)
AG_GST_CHECK_FEATURE(CELT, [celt], celt, [
  PKG_CHECK_MODULES(CELT, celt >= 0.4.0, [
          HAVE_CELT="yes"
  ], [
          AC_MSG_RESULT(no)
	  HAVE_CELT="no"
  ])

  PKG_CHECK_MODULES(CELT_0_5, celt >= 0.5.0, [
          AC_DEFINE([HAVE_CELT_0_5], 1, [Define if libcelt 0.5 is installed])
  ], [
          AC_MSG_RESULT(no)
  ])

  PKG_CHECK_MODULES(CELT_0_7, celt >= 0.7.0, [
          AC_DEFINE([HAVE_CELT_0_7], 1, [Define if libcelt 0.7 is installed])
  ], [
          AC_MSG_RESULT(no)
  ])
  AC_SUBST(CELT_CFLAGS)
  AC_SUBST(CELT_LIBS)
])

dnl *** Cog ***
translit(dnm, m, l) AM_CONDITIONAL(USE_COG, true)
AG_GST_CHECK_FEATURE(COG, [Cog plugin], cog, [
  PKG_CHECK_MODULES(COG, libpng12 orc-0.4 >= 0.4.3, HAVE_COG="yes", [
    HAVE_COG="no"
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(COG_CFLAGS)
  AC_SUBST(COG_LIBS)
  ORCC=`$PKG_CONFIG --variable=orcc orc-0.4`
  if test "x$ORCC" = x ; then
    ORCC=orcc
  fi
  AC_SUBST(ORCC)
])

dnl *** dc1394  ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DC1394, true)
AG_GST_CHECK_FEATURE(DC1394, [libdc1394], dc1394, [
   PKG_CHECK_MODULES(LIBDC1394, libdc1394-2 >= 2.0.0, [
   	if test -z "`pkg-config --modversion libdc1394-2 | grep 2.0.0-rc`"; then
		HAVE_DC1394="yes"
	else
		HAVE_DC1394="no"
		AC_MSG_RESULT(no)
	fi
   ], [
        HAVE_DC1394="no"
	AC_MSG_RESULT(no)
   ])
   AC_SUBST(LIBDC1394_CFLAGS)
   AC_SUBST(LIBDC1394_LIBS)
])


dnl **** DirectFB ****
translit(dnm, m, l) AM_CONDITIONAL(USE_DIRECTFB, true)
AG_GST_CHECK_FEATURE(DIRECTFB, [directfb], dfbvideosink , [
  PKG_CHECK_MODULES(DIRECTFB, directfb >= 0.9.24, HAVE_DIRECTFB="yes", [
    HAVE_DIRECTFB="no"
    AC_MSG_RESULT(no)
  ])
])

dnl **** Dirac ****
translit(dnm, m, l) AM_CONDITIONAL(USE_DIRAC, true)
AG_GST_CHECK_FEATURE(DIRAC, [dirac], dirac, [
  PKG_CHECK_MODULES(DIRAC, dirac >= 0.10, HAVE_DIRAC="yes", [
    HAVE_DIRAC="no"
    AC_MSG_RESULT(no)
  ])
])

dnl *** DTS ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DTS, true)
AG_GST_CHECK_FEATURE(DTS, [dts library], dtsdec, [
  HAVE_DTS="yes"
  AG_GST_CHECK_LIBHEADER(DTS, dca, dca_init, -lm, dca.h,
    DTS_LIBS="-ldca -lm", HAVE_DTS="no")
  if test $HAVE_DTS = "no"; then
    AG_GST_CHECK_LIBHEADER(DTS, dts_pic, dts_init, -lm, dts.h,
      [ DTS_LIBS="-ldts_pic -lm"
        AC_DEFINE(DTS_OLD, 1, [Define if an old libdts is used])
      ])
  fi
  AC_SUBST(DTS_LIBS)
])

dnl *** DIVX ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DIVX, true)
AG_GST_CHECK_FEATURE(DIVX, [divx plugins], divx, [
  HAVE_DIVX=yes
  AC_CHECK_HEADER(encore2.h, , [
    AC_MSG_WARN([Divx4linux encore headers not found])
    HAVE_DIVX=no
  ])
  if [ test x$HAVE_DIVX = xyes ]; then
    AC_MSG_CHECKING([Checking for valid divx4linux encore version])
    AC_TRY_COMPILE([
#include <encore2.h>
#if ENCORE_VERSION != 20021024
#error Wrong version of divx encore libraries
#endif
    ], [
return 0;
    ], [
      HAVE_DIVX=yes
      AC_MSG_RESULT(yes)
    ], [
      HAVE_DIVX=no
      AC_MSG_RESULT(no)
      AC_MSG_WARN([Wrong version of divx4linux installed])
    ])
  fi
  if [ test x$HAVE_DIVX = xyes ]; then
    AC_CHECK_HEADER(decore.h, , [
      AC_MSG_WARN([Divx4linux decoder headers not found])
      HAVE_DIVX=no
    ])
  fi
  if [ test x$HAVE_DIVX = xyes ]; then
    AC_MSG_CHECKING([Checking for valid divx4linux decore version])
    AC_TRY_COMPILE([
#include <decore.h>
#if DECORE_VERSION != 20021112
#error Wrong version of divx decore libraries
#endif
    ], [
return 0;
    ], [
      HAVE_DIVX=yes
      AC_MSG_RESULT(yes)
    ], [
      HAVE_DIVX=no
      AC_MSG_RESULT(no)
      AC_MSG_WARN([Wrong version of divx4linux installed])
    ])
  fi
  LIBS="-lm"
  if test x$HAVE_DIVX = xyes; then
    AC_CHECK_LIB(divxencore, encore, , [
      AC_MSG_WARN([Divx4linux encore libs not found])
      HAVE_DIVX=no
    ])
  fi
  if test x$HAVE_DIVX = xyes; then
    AC_CHECK_LIB(divxdecore, decore, , [
      AC_MSG_WARN([Divx4linux decore libs not found])
      HAVE_DIVX=no
  ])
  fi
  if test x$HAVE_DIVX = xyes; then
    DIVXENC_LIBS="-ldivxencore -lm"
    DIVXDEC_LIBS="-ldivxdecore -lm"
    AC_SUBST(DIVXENC_LIBS)
    AC_SUBST(DIVXDEC_LIBS)
  fi
])

dnl *** dvdnav for resindvd ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DVDNAV, true)
AG_GST_CHECK_FEATURE(DVDNAV, [dvdnav library], resindvd, [
  translit(dnm, m, l) AC_SUBST(DVDNAV_LIBS)
  translit(dnm, m, l) AC_SUBST(DVDNAV_CFLAGS)
  AG_GST_CHECK_CONFIGPROG(DVDNAV, dvdnav-config)
  if test x"$HAVE_DVDNAV" = x"yes"; then
    dnl check version
    DVDNAV_VERSION=`dvdnav-config --version|head -n 1|sed 's/^.*) //'|sed 's/ (.*)//'`
    DVDNAV_MAJOR=`echo $DVDNAV_VERSION | cut -d. -f1 | sed s/[a-zA-Z\-].*//g`
    DVDNAV_MINOR=`echo $DVDNAV_VERSION | cut -d. -f2 | sed s/[a-zA-Z\-].*//g`
    DVDNAV_MICRO=`echo $DVDNAV_VERSION | cut -d. -f3 | sed s/[a-zA-Z\-].*//g`
    if [[ "$DVDNAV_MAJOR" -lt "4" ]]; then
      AC_MSG_WARN([libdvdnav >= 4.1.2 is required, you have $DVDNAV_VERSION])
      HAVE_DVDNAV="no"
    elif [[ "$DVDNAV_MAJOR" -eq "4" ]] && \
         [[ "$DVDNAV_MINOR" -lt "1" ]]; then
      AC_MSG_WARN([libdvdnav >= 4.1.2 is required, you have $DVDNAV_VERSION])
      HAVE_DVDNAV="no"
    elif [[ "$DVDNAV_MAJOR" -eq "4" ]] && \
         [[ "$DVDNAV_MINOR" -eq "1" ]] && \
         [[ "$DVDNAV_MICRO" -lt "2" ]]; then
      AC_MSG_WARN([libdvdnav >= 4.1.2 is required, you have $DVDNAV_VERSION])
      HAVE_DVDNAV="no"
    fi
  fi
  AS_SCRUB_INCLUDE(DVDNAV_CFLAGS)
])

dnl *** METADATA ***
translit(dnm, m, l) AM_CONDITIONAL(USE_METADATA, true)
AG_GST_CHECK_FEATURE(METADATA, [METADATA muxer and demuxer], metadata, [
  HAVE_METADATA="no"
  PKG_CHECK_MODULES(EXIF, libexif >= 0.6.16, HAVE_EXIF="yes", [
    HAVE_EXIF="no"
  ])
  PKG_CHECK_MODULES(IPTC, libiptcdata >= 1.0.2, HAVE_IPTC="yes", [
    HAVE_IPTC="no"
  ])
  PKG_CHECK_MODULES(XMP, exempi-2.0, HAVE_XMP="yes", [
    HAVE_XMP="no"
  ])
  PKG_CHECK_MODULES(XMP_1_99_5, exempi-2.0 >= 1.99.5, XMP_CFLAGS="-DXMP_1_99_5 $XMP_CFLAGS", AC_MSG_RESULT(no))
  if test x$HAVE_EXIF = xyes; then
    METADATA_CFLAGS="-DHAVE_EXIF $EXIF_CFLAGS $METADATA_CFLAGS"
    METADATA_LIBS="$EXIF_LIBS $METADATA_LIBS -lm"
    HAVE_METADATA="yes"
  fi
  if test x$HAVE_IPTC = xyes; then
    METADATA_CFLAGS="-DHAVE_IPTC $IPTC_CFLAGS $METADATA_CFLAGS"
    METADATA_LIBS="$IPTC_LIBS $METADATA_LIBS"
    HAVE_METADATA="yes"
  fi
  if test x$HAVE_XMP = xyes; then
    METADATA_CFLAGS="-DHAVE_XMP $XMP_CFLAGS $METADATA_CFLAGS"
    METADATA_LIBS="$XMP_LIBS $METADATA_LIBS"
    HAVE_METADATA="yes"
  fi
  if test x$HAVE_METADATA = xno; then
    AC_MSG_WARN(no of metadata libraries (exif, iptc or xmp) found)
  else
    HAVE_METADATA="yes"
  fi
  AC_SUBST(METADATA_CFLAGS)
  AC_SUBST(METADATA_LIBS)
])

dnl **** Free AAC Encoder (FAAC) ****
translit(dnm, m, l) AM_CONDITIONAL(USE_FAAC, true)
AG_GST_CHECK_FEATURE(FAAC, [AAC encoder plug-in], faac, [
  AG_GST_CHECK_LIBHEADER(FAAC, faac, faacEncOpen, -lm, faac.h,
    FAAC_LIBS="-lfaac -lm")
  AS_SCRUB_INCLUDE(FAAC_CFLAGS)
  AC_SUBST(FAAC_LIBS)
])

dnl **** Free AAC Decoder (FAAD) ****
translit(dnm, m, l) AM_CONDITIONAL(USE_FAAD, true)
AG_GST_CHECK_FEATURE(FAAD, [AAC decoder plug-in], faad, [
  HAVE_FAAD="yes"
  faad_hdr=""
  AG_GST_CHECK_LIBHEADER(FAAD, faad, faacDecOpen, -lm, faad.h,
    FAAD_LIBS="-lfaad -lm", HAVE_FAAD="no")
  if test $HAVE_FAAD = "no"; then
    AG_GST_CHECK_LIBHEADER(FAAD, faad, NeAACDecOpen, -lm, neaacdec.h,
      [ FAAD_LIBS="-lfaad -lm"
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
    AC_TRY_COMPILE([#include <faad.h>], [
        #if !defined(FAAD2_VERSION) || !defined(FAAD_FMT_DOUBLE)
        #error Not faad2
        #else
        /* Release candidate of 2.0 is not good enough for us. This field
         * was added only in 2.0 final and does not exist in 2.0-RC3 */
        faacDecConfiguration conf; conf.dontUpSampleImplicitSBR = 1;
        #endif
    ], [
      HAVE_FAAD="yes"
      AC_MSG_RESULT(yes)

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
    ], [
      HAVE_FAAD="no"
      AC_MSG_RESULT(no)
    ])
  fi
  AS_SCRUB_INCLUDE(FAAD_CFLAGS)
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

dnl *** ivorbis ***
dnl AM_PATH_IVORBIS only takes two options
translit(dnm, m, l) AM_CONDITIONAL(USE_IVORBIS, true)
AG_GST_CHECK_FEATURE(IVORBIS, [integer vorbis plug-in], ivorbisdec, [
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
])

dnl *** Jack ***
translit(dnm, m, l) AM_CONDITIONAL(USE_JACK, true)
AG_GST_CHECK_FEATURE(JACK, Jack, jack, [
  PKG_CHECK_MODULES(JACK, jack >= 0.99.10, HAVE_JACK="yes", HAVE_JACK="no")
  AC_SUBST(JACK_CFLAGS)
  AC_SUBST(JACK_LIBS)
])

dnl *** jp2k ***
translit(dnm, m, l) AM_CONDITIONAL(USE_JP2K, true)
AG_GST_CHECK_FEATURE(JP2K, [jp2k], jp2kdec jp2kenc, [
  AG_GST_CHECK_LIBHEADER(JP2K, jasper, jas_stream_fopen, , jasper/jasper.h,
    JP2K_LIBS="-ljasper")
  AC_SUBST(JP2K_LIBS)
])

dnl *** kate ***
translit(dnm, m, l) AM_CONDITIONAL(USE_KATE, true)
AG_GST_CHECK_FEATURE(KATE, [Kate], kate, [
  PKG_CHECK_MODULES(KATE, kate >= 0.1.7, HAVE_KATE="yes", [
    HAVE_KATE="no"
    AC_MSG_RESULT(no)
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
      AC_MSG_RESULT(no)
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
    AC_MSG_RESULT(no)
  ])
])
AC_SUBST(LIBMMS_LIBS)

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
  MIMDEC_REQUIRED=1.0
  PKG_CHECK_MODULES(MIMIC, libmimic >= $MIMDEC_REQUIRED,
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
    PKG_CHECK_MODULES(MJPEG, mjpegtools >= 1.9.0, [
      dnl logging API changed in release candidates
      OLD_CFLAGS="$CFLAGS"
      OLD_LIBS="$LIBS"
      CFLAGS="$MJPEG_CFLAGS"
      LIBS="$LIBS $MJPEG_LIBS -lmjpegutils -lm -lpthread"
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
      AC_LANG_CPLUSPLUS
      OLD_CPPFLAGS="$CPPFLAGS"
      dnl HACK as above
      CPPFLAGS_GOOD="$CPPFLAGS $MPEG2ENC_CFLAGS"
      CPPFLAGS="$CPPFLAGS_GOOD -I."
      dnl check headers
      mpeg2enc_headers_ok=no
      AC_CHECK_HEADER([mpeg2encoder.hh], [
        MPEG2ENC_LIBS="$MPEG2ENC_LIBS -lmpeg2encpp -lm -lpthread"
        OLD_LIBS="$LIBS"
        LIBS="$LIBS $MPEG2ENC_LIBS"
        AC_MSG_CHECKING([for valid mpeg2enc objects])
        AC_TRY_RUN([
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
        ],[
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
          CPP_FLAGS="$CPPFLAGS_GOOD"
          AC_SUBST(MPEG2ENC_CFLAGS)
          AC_SUBST(MPEG2ENC_LIBS)
        ], [
          AC_MSG_RESULT(no)
        ])
        LIBS="$OLD_LIBS"
      ])

      CPPFLAGS="$OLD_CPPFLAGS"
      AC_LANG_C
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
      dnl switch over to c++ to test things
      AC_LANG_CPLUSPLUS
      OLD_CPPFLAGS="$CPPFLAGS"
      CPPFLAGS="$CPPFLAGS $MPLEX_CFLAGS"
      AC_CHECK_HEADER(interact.hpp, [
        MPLEX_LIBS="$MPLEX_LIBS -lmplex2 -lm"
        OLD_LIBS="$LIBS"
        LIBS="$LIBS $MPLEX_LIBS"
        dnl older libmplex uses off_t SegmentSize (), which leads to fuzzy ABI;
        dnl don't want this here
        AC_MSG_CHECKING([for valid mplex objects])
        AC_TRY_RUN([

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
        ],[
          HAVE_MPLEX="yes"
          AC_SUBST(MPLEX_CFLAGS)
          AC_SUBST(MPLEX_LIBS)
          AC_MSG_RESULT(yes)
        ], AC_MSG_RESULT(no))
        LIBS="$OLD_LIBS"
      ])
      CPPFLAGS="$OLD_CPPFLAGS"
      AC_LANG_C
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

dnl *** musicbrainz ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MUSICBRAINZ, true)
AG_GST_CHECK_FEATURE(MUSICBRAINZ, [musicbrainz tag generation], musicbrainz, [
  PKG_CHECK_MODULES(MUSICBRAINZ, libmusicbrainz >= 2.1.0, HAVE_MUSICBRAINZ="yes", HAVE_MUSICBRAINZ="no")
  AC_SUBST(MUSICBRAINZ_CFLAGS)
  AC_SUBST(MUSICBRAINZ_LIBS)
])

dnl *** MythTV ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MYTHTV, true)
AG_GST_CHECK_FEATURE(MYTHTV, [MythTV client plugins], mythtvsrc, [
  PKG_CHECK_MODULES(GMYTH, gmyth >= 0.4 gmyth <= 0.7.99, HAVE_MYTHTV="yes", [
    HAVE_MYTHTV="no"
    AC_MSG_RESULT(no)
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
  PKG_CHECK_MODULES(NEON, neon >= 0.26.0  neon <= 0.29.99, HAVE_NEON="yes", [
    HAVE_NEON="no"
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(NEON_CFLAGS)
  AC_SUBST(NEON_LIBS)
])

dnl *** ofa ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OFA, true)
AG_GST_CHECK_FEATURE(OFA, [ofa plugins], ofa, [
  PKG_CHECK_MODULES(OFA, libofa >= 0.9.3, HAVE_OFA="yes", [
    HAVE_OFA="no"
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(OFA_CFLAGS)
  AC_SUBST(OFA_LIBS)
])

dnl *** rsvg ***
translit(dnm, m, l) AM_CONDITIONAL(USE_RSVG, true)
AG_GST_CHECK_FEATURE(RSVG, [rsvg decoder], rsvg, [
  PKG_CHECK_MODULES(RSVG, librsvg-2.0 >= 2.14 cairo, HAVE_RSVG="yes", [
    HAVE_RSVG="no"
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(RSVG_CFLAGS)
  AC_SUBST(RSVG_LIBS)
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
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(TIMIDITY_CFLAGS)
  AC_SUBST(TIMIDITY_LIBS)
])

dnl *** wildmidi ***
translit(dnm, m, l) AM_CONDITIONAL(USE_WILDMIDI, true)
AG_GST_CHECK_FEATURE(WILDMIDI, [wildmidi midi soft synth plugin], wildmidi, [
  AC_CHECK_HEADER([wildmidi_lib.h],HAVE_WILDMIDI=yes, HAVE_WILDMIDI=no)
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

dnl *** sndfile ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SNDFILE, true)
AG_GST_CHECK_FEATURE(SNDFILE, [sndfile plug-in], sfsrc sfsink, [
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
  PKG_CHECK_MODULES(SOUNDTOUCH, soundtouch-1.4,
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
          HAVE_SOUNDTOUCH=no)])])])
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

dnl *** swfdec ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SWFDEC, true)
AG_GST_CHECK_FEATURE(SWFDEC, [swfdec plug-in], swfdec, [
  PKG_CHECK_MODULES(SWFDEC, swfdec-0.3 >= 0.3.6, HAVE_SWFDEC=yes, [
    HAVE_SWFDEC=no
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(SWFDEC_CFLAGS)
  AC_SUBST(SWFDEC_LIBS)
])

dnl *** theora ***
translit(dnm, m, l) AM_CONDITIONAL(USE_THEORADEC, true)
AG_GST_CHECK_FEATURE(THEORADEC, [ogg theora codec], theoraexpdec, [
  PKG_CHECK_MODULES(THEORADEC, theoradec, HAVE_THEORADEC="yes", [
    HAVE_THEORADEC="no"
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(THEORADEC_LIBS)
  AC_SUBST(THEORADEC_CFLAGS)
])

dnl *** XVID ***
translit(dnm, m, l) AM_CONDITIONAL(USE_XVID, true)
AG_GST_CHECK_FEATURE(XVID, [xvid plugins], xvid, [
  HAVE_XVID=no
  AC_CHECK_HEADER(xvid.h, [
    OLD_LIBS="$LIBS"
    LIBS="-lm"
    AC_CHECK_LIB(xvidcore, xvid_encore, [
      AC_CHECK_LIB(xvidcore, xvid_decore, [
        AC_CHECK_LIB(xvidcore, xvid_global, [
          AC_MSG_CHECKING([for uptodate XviD API version])
          AC_TRY_COMPILE([#include <xvid.h>], [
              #if XVID_API_MAJOR(XVID_API) != 4
              #error "Incompatible XviD API version"
              #endif
          ],[ AC_MSG_RESULT(yes)
            XVID_LIBS="-lxvidcore -lm"
            AC_SUBST(XVID_LIBS)
            HAVE_XVID=yes
          ], AC_MSG_RESULT(no) )
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
  AC_CHECK_HEADER(linux/dvb/frontend.h, [HAVE_DVB="yes"], [HAVE_DVB="no"])
])

dnl *** oss4 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OSS4, true)
AG_GST_CHECK_FEATURE(OSS4, [Open Sound System 4], oss4, [
  AC_MSG_CHECKING([Checking if we can build the OSS4 elements])
  AC_CHECK_HEADERS([fcntl.h sys/ioctl.h sys/stat.h sys/types.h],
      [test -z "$OSS4_MISSING_HEADER" && HAVE_OSS4="yes"],
      [OSS4_MISSING_HEADER="yes";HAVE_OSS4="no"])
])

dnl *** wininet ***
translit(dnm, m, l) AM_CONDITIONAL(USE_WININET, true)
AG_GST_CHECK_FEATURE(WININET, [Windows internet library], wininet, [
  AC_MSG_CHECKING([Checking for windows internet support])
  AC_CHECK_HEADERS([windows.h wininet.h],
      [HAVE_WININET="yes"], [HAVE_WININET="no"])
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

dnl *** schroedinger ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SCHRO, true)
AG_GST_CHECK_FEATURE(SCHRO, [Schroedinger video codec], schro, [
  AG_GST_PKG_CHECK_MODULES(SCHRO, schroedinger-1.0 >= 1.0.7)
])

dnl *** zbar ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ZBAR, true)
AG_GST_CHECK_FEATURE(ZBAR, [ZBar barcode detector], zbar, [
  AG_GST_PKG_CHECK_MODULES(ZBAR, zbar >= 0.9)
])

else

dnl not building plugins with external dependencies,
dnl but we still need to set the conditionals

AM_CONDITIONAL(USE_ALSA, false)
AM_CONDITIONAL(USE_ASSRENDER, false)
AM_CONDITIONAL(USE_AMRWB, false)
AM_CONDITIONAL(USE_APEXSINK, false)
AM_CONDITIONAL(USE_BZ2, false)
AM_CONDITIONAL(USE_CDAUDIO, false)
AM_CONDITIONAL(USE_CELT, false)
AM_CONDITIONAL(USE_COG, false)
AM_CONDITIONAL(USE_DC1394, false)
AM_CONDITIONAL(USE_DIRECTFB, false)
AM_CONDITIONAL(USE_DIRAC, false)
AM_CONDITIONAL(USE_DTS, false)
AM_CONDITIONAL(USE_DIVX, false)
AM_CONDITIONAL(USE_DVDNAV, false)
AM_CONDITIONAL(USE_METADATA, false)
AM_CONDITIONAL(USE_FAAC, false)
AM_CONDITIONAL(USE_FAAD, false)
AM_CONDITIONAL(USE_FBDEV, false)
AM_CONDITIONAL(USE_GSM, false)
AM_CONDITIONAL(USE_IVORBIS, false)
AM_CONDITIONAL(USE_JACK, false)
AM_CONDITIONAL(USE_JP2K, false)
AM_CONDITIONAL(USE_KATE, false)
AM_CONDITIONAL(USE_TIGER, false)
AM_CONDITIONAL(USE_LADSPA, false)
AM_CONDITIONAL(USE_LV2, false)
AM_CONDITIONAL(USE_LIBMMS, false)
AM_CONDITIONAL(USE_MODPLUG, false)
AM_CONDITIONAL(USE_MIMIC, false)
AM_CONDITIONAL(USE_MPEG2ENC, false)
AM_CONDITIONAL(USE_MPLEX, false)
AM_CONDITIONAL(USE_MUSEPACK, false)
AM_CONDITIONAL(USE_MUSICBRAINZ, false)
AM_CONDITIONAL(USE_MYTHTV, false)
AM_CONDITIONAL(USE_NAS, false)
AM_CONDITIONAL(USE_NEON, false)
AM_CONDITIONAL(USE_OFA, false)
AM_CONDITIONAL(USE_RSVG, false)
AM_CONDITIONAL(USE_TIMIDITY, false)
AM_CONDITIONAL(USE_WILDMIDI, false)
AM_CONDITIONAL(USE_SDL, false)
AM_CONDITIONAL(USE_SNDFILE, false)
AM_CONDITIONAL(USE_SOUNDTOUCH, false)
AM_CONDITIONAL(USE_SPC, false)
AM_CONDITIONAL(USE_GME, false)
AM_CONDITIONAL(USE_SWFDEC, false)
AM_CONDITIONAL(USE_THEORADEC, false)
AM_CONDITIONAL(USE_XVID, false)
AM_CONDITIONAL(USE_DVB, false)
AM_CONDITIONAL(USE_OSS4, false)
AM_CONDITIONAL(USE_WININET, false)
AM_CONDITIONAL(USE_ACM, false)
AM_CONDITIONAL(USE_VDPAU, false)
AM_CONDITIONAL(USE_SCHRO, false)
AM_CONDITIONAL(USE_ZBAR, false)

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
GST_OPTION_CFLAGS="\$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
GST_OPTION_CXXFLAGS="\$(ERROR_CXXFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
AC_SUBST(GST_OPTION_CFLAGS)
AC_SUBST(GST_OPTION_CXXFLAGS)

dnl FIXME: do we want to rename to GST_ALL_* ?
dnl prefer internal headers to already installed ones
dnl also add builddir include for enumtypes and marshal
dnl add GST_OPTION_CFLAGS, but overridable
GST_CFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs $GST_CFLAGS \$(GST_OPTION_CFLAGS)"
GST_CXXFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs $GST_CXXFLAGS \$(GST_OPTION_CXXFLAGS)"
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_CXXFLAGS)
AC_SUBST(GST_LIBS)

dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_ALL_LDFLAGS="-no-undefined"
AC_SUBST(GST_ALL_LDFLAGS)

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_desc\$\$' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

SHAVE_INIT([common],[enable])

dnl *** output files ***

dnl po/Makefile.in

AC_CONFIG_FILES(
Makefile
common/Makefile
common/shave
common/shave-libtool
common/m4/Makefile
gst-plugins-bad.spec
gst/Makefile
gst/adpcmdec/Makefile
gst/adpcmenc/Makefile
gst/aiff/Makefile
gst/asfmux/Makefile
gst/audioparsers/Makefile
gst/autoconvert/Makefile
gst/bayer/Makefile
gst/camerabin/Makefile
gst/cdxaparse/Makefile
gst/dccp/Makefile
gst/debugutils/Makefile
gst/dtmf/Makefile
gst/dvdspu/Makefile
gst/festival/Makefile
gst/freeze/Makefile
gst/frei0r/Makefile
gst/h264parse/Makefile
gst/hdvparse/Makefile
gst/id3tag/Makefile
gst/legacyresample/Makefile
gst/librfb/Makefile
gst/liveadder/Makefile
gst/mpegdemux/Makefile
gst/mpegtsmux/Makefile
gst/mpegtsmux/tsmux/Makefile
gst/mpegpsmux/Makefile
gst/mpeg4videoparse/Makefile
gst/mpegvideoparse/Makefile
gst/mve/Makefile
gst/mxf/Makefile
gst/nsf/Makefile
gst/nuvdemux/Makefile
gst/pcapparse/Makefile
gst/pnm/Makefile
gst/qtmux/Makefile
gst/rawparse/Makefile
gst/real/Makefile
gst/rtpmux/Makefile
gst/scaletempo/Makefile
gst/sdp/Makefile
gst/selector/Makefile
gst/shapewipe/Makefile
gst/siren/Makefile
gst/speed/Makefile
gst/subenc/Makefile
gst/stereo/Makefile
gst/tta/Makefile
gst/valve/Makefile
gst/videomeasure/Makefile
gst/videosignal/Makefile
gst/vmnc/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/interfaces/Makefile
gst-libs/gst/signalprocessor/Makefile
gst-libs/gst/video/Makefile
sys/Makefile
sys/dshowdecwrapper/Makefile
sys/acmenc/Makefile
sys/acmmp3dec/Makefile
sys/directdraw/Makefile
sys/directsound/Makefile
sys/dshowsrcwrapper/Makefile
sys/dshowvideosink/Makefile
sys/dvb/Makefile
sys/fbdev/Makefile
sys/oss4/Makefile
sys/osxvideo/Makefile
sys/qtwrapper/Makefile
sys/vcd/Makefile
sys/vdpau/Makefile
sys/wasapi/Makefile
sys/wininet/Makefile
sys/winks/Makefile
sys/winscreencap/Makefile
tests/Makefile
tests/check/Makefile
tests/examples/Makefile
tests/examples/camerabin/Makefile
tests/examples/directfb/Makefile
tests/examples/mxf/Makefile
tests/examples/scaletempo/Makefile
tests/examples/shapewipe/Makefile
tests/examples/switch/Makefile
tests/icles/Makefile
ext/alsaspdif/Makefile
ext/amrwbenc/Makefile
ext/assrender/Makefile
ext/apexsink/Makefile
ext/bz2/Makefile
ext/cdaudio/Makefile
ext/celt/Makefile
ext/cog/Makefile
ext/dc1394/Makefile
ext/dirac/Makefile
ext/directfb/Makefile
ext/divx/Makefile
ext/dts/Makefile
ext/faac/Makefile
ext/faad/Makefile
ext/gsm/Makefile
ext/ivorbis/Makefile
ext/jack/Makefile
ext/jp2k/Makefile
ext/kate/Makefile
ext/ladspa/Makefile
ext/lv2/Makefile
ext/libmms/Makefile
ext/Makefile
ext/metadata/Makefile
ext/modplug/Makefile
ext/mpeg2enc/Makefile
ext/mimic/Makefile
ext/mplex/Makefile
ext/musepack/Makefile
ext/musicbrainz/Makefile
ext/mythtv/Makefile
ext/nas/Makefile
ext/neon/Makefile
ext/ofa/Makefile
ext/rsvg/Makefile
ext/resindvd/Makefile
ext/schroedinger/Makefile
ext/sdl/Makefile
ext/sndfile/Makefile
ext/soundtouch/Makefile
ext/gme/Makefile
ext/spc/Makefile
ext/swfdec/Makefile
ext/theora/Makefile
ext/timidity/Makefile
ext/xvid/Makefile
ext/zbar/Makefile
po/Makefile.in
docs/Makefile
docs/plugins/Makefile
docs/plugins/figures/Makefile
docs/version.entities
pkgconfig/Makefile
pkgconfig/gstreamer-plugins-bad.pc
pkgconfig/gstreamer-plugins-bad-uninstalled.pc
m4/Makefile
win32/common/config.h
)
AC_OUTPUT

AG_GST_OUTPUT_PLUGINS
