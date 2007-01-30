AC_PREREQ(2.52)

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT(GStreamer Bad Plug-ins, 0.10.4.1,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gst-plugins-bad)

dnl initialize automake
AM_INIT_AUTOMAKE

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_CVS="no", GST_CVS="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/qtdemux/qtdemux.c])

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

AM_PROG_LIBTOOL

dnl *** required versions of GStreamer stuff ***
GST_REQ=0.10.10.1
GSTPB_REQ=0.10.10.1

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl set up gettext
dnl the version check needs to stay here because autopoint greps for it
AM_GNU_GETTEXT_VERSION([0.11.5])
AM_GNU_GETTEXT([external])
GST_GETTEXT([gst-plugins-bad-$GST_MAJORMINOR])

dnl *** check for arguments to configure ***

GST_ARG_DEBUG
GST_ARG_PROFILING
GST_ARG_VALGRIND
GST_ARG_GCOV

GST_ARG_EXAMPLES

GST_ARG_WITH_PKG_CONFIG_PATH
GST_ARG_WITH_PACKAGE_NAME
GST_ARG_WITH_PACKAGE_ORIGIN

GST_ARG_EXAMPLES

dnl these are all the gst plug-ins, compilable without additional libs
GST_PLUGINS_ALL="\
  cdxaparse \
  deinterlace \
  filter \
  freeze \
  h264parse \
  interleave \
  librfb \
  nuvdemux \
  modplug \
  multifile \
  mve \
  nsf \
  replaygain \
  spectrum \
  speed \
  qtdemux \
  tta \
  videocrop \
  videoparse \
  xingheader \
  y4m \
  real \
  "

AC_SUBST(GST_PLUGINS_ALL)

GST_ARG_WITH_PLUGINS

GST_ARG_ENABLE_EXTERNAL

GST_ARG_ENABLE_EXPERIMENTAL

USE_TARKIN=$BUILD_EXPERIMENTAL

dnl *** checks for platform ***

dnl * hardware/architecture *

dnl common/m4/gst-arch.m4
dnl check CPU type
GST_ARCH

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

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for documentation tools
GTK_DOC_CHECK([1.3])
AS_PATH_PYTHON([2.1])

dnl *** checks for libraries ***

dnl *** checks for header files ***

dnl used by ext/dts
AX_CREATE_STDINT_H

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***

dnl *** checks for dependancy libraries ***

dnl GLib is required
GST_GLIB_CHECK([2.6])

dnl liboil is required
PKG_CHECK_MODULES(LIBOIL, liboil-0.3 >= 0.3.2, HAVE_LIBOIL=yes, HAVE_LIBOIL=no)
if test "x${HAVE_LIBOIL}" != xyes ; then
  AC_ERROR([liboil-0.3 is required])
fi

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
GST_CHECK_GST($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_BASE($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_GDP($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_CHECK($GST_MAJORMINOR, [$GST_REQ], no)
GST_CHECK_GST_CONTROLLER($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_PLUGINS_BASE($GST_MAJORMINOR, [$GSTPB_REQ], no)
GSTPB_PLUGINS_DIR=`$PKG_CONFIG gstreamer-plugins-base-$GST_MAJORMINOR --variable pluginsdir`
AC_SUBST(GSTPB_PLUGINS_DIR)
AC_MSG_NOTICE(Using GStreamer Base Plugins in $GSTPB_PLUGINS_DIR)

dnl FIXME: get rid of this by making sure gstreamer-check brings it in
dnl check for "check", unit testing library/header
AM_PATH_CHECK(0.9.2, HAVE_CHECK=yes, HAVE_CHECK=no)
AM_CONDITIONAL(HAVE_CHECK, test "x$HAVE_CHECK" = "xyes")

dnl GTK is optional and used in examples
HAVE_GTK=NO
PKG_CHECK_MODULES(GTK2, gtk+-2.0 >= 2.2.0, HAVE_GTK_22=yes, HAVE_GTK_22=no)
if test "x$HAVE_GTK_22" = "xyes"; then
  HAVE_GTK=yes
  GTK_VERSION=`$PKG_CONFIG --variable=gtk_binary_version gtk+-2.0`
  AC_SUBST(GTK_VERSION)
  GTK_PREFIX=`$PKG_CONFIG --variable=prefix gdk-pixbuf-2.0`
  AC_SUBST(GTK_BASE_DIR)
  GDK_PIXBUF_LIBDIR=`$PKG_CONFIG --variable=libdir gdk-pixbuf-2.0`
  GDK_PIXBUF_PREFIXDIR=`$PKG_CONFIG --variable=prefix gdk-pixbuf-2.0`
  AC_SUBST(GTK_BASE_DIR)
else
  PKG_CHECK_MODULES(GTK2, gtk+-2.0, HAVE_GTK_20=yes, HAVE_GTK_20=no)
fi
if test "x$HAVE_GTK_20" = "xyes"; then
  HAVE_GTK=yes
fi
GTK_CFLAGS=$GTK2_CFLAGS
GTK_LIBS=$GTK2_LIBS
AC_SUBST(GTK_LIBS)
AC_SUBST(GTK_CFLAGS)
AC_SUBST(HAVE_GTK)
AM_CONDITIONAL(HAVE_GTK, test "x$HAVE_GTK" = "xyes")

dnl set license and copyright notice
GST_LICENSE="LGPL"
AC_DEFINE_UNQUOTED(GST_LICENSE, "$GST_LICENSE", [GStreamer license])
AC_SUBST(GST_LICENSE)

dnl set location of plugin directory
GST_SET_PLUGINDIR

dnl define an ERROR_CFLAGS Makefile variable
GST_SET_ERROR_CFLAGS($GST_CVS)

dnl define correct level for debugging messages
GST_SET_LEVEL_DEFAULT($GST_CVS)

dnl *** plug-ins to exclude ***

dnl see if we can build C++ plug-ins
if test "x$HAVE_CXX" != "xyes"; then
  AC_MSG_WARN([Not compiling plug-ins requiring C++ compiler])
  GST_PLUGINS_SELECTED=`echo $GST_PLUGINS_SELECTED | sed 's/modplug//g'`
fi

dnl real plugin only works on i386 and x86_64 for the time being.
if test "x$HAVE_CPU_I386" != "xyes" && test "x$HAVE_CPU_X86_64" != "xyes"; then
  AC_MSG_WARN([Not building real plugin, only works on 32bit and 64bit x86 platforms])
  GST_PLUGINS_SELECTED=`echo $GST_PLUGINS_SELECTED | sed 's/real//g'`
fi

dnl disable gst plugins we might not be able to build on this
dnl platform: librfb (ugly but minimally invasive)
dnl FIXME: maybe move to sys, or make work with winsock2
AC_CHECK_HEADERS([sys/socket.h], HAVE_SYS_SOCKET_H=yes)
AC_CHECK_HEADERS([winsock2.h], HAVE_WINSOCK2_H=yes)

if test "x$HAVE_SYS_SOCKET_H" != "xyes"; then
  GST_PLUGINS_SELECTED=`echo $GST_PLUGINS_SELECTED | $SED -e s/librfb//`
fi

AC_SUBST(GST_PLUGINS_SELECTED)


dnl *** sys plug-ins ***

dnl OpenGL
translit(dnm, m, l) AM_CONDITIONAL(USE_OPENGL, true)
GST_CHECK_X
save_CPPFLAGS="$CPPFLAGS"
save_LIBS="$LIBS"
CPPFLAGS="$CPPFLAGS $X_CFLAGS"
LIBS="$LIBS $X_LIBS"
GST_CHECK_FEATURE(OPENGL, [Open GL], glsink, [
  GST_CHECK_LIBHEADER(GL, GL, glTexImage2D,,
                      GL/gl.h, HAVE_OPENGL="yes", HAVE_OPENGL="no")
])
CPPFLAGS="$save_CPPFLAGS"
LIBS="$save_LIBS"

dnl Check for X11
translit(dnm, m, l) AM_CONDITIONAL(USE_X, true)
GST_CHECK_FEATURE(X, [X libraries and plugins],
                  [ximagesrc], [
  AC_PATH_XTRA

  dnl now try to find the HEADER
  ac_cflags_save="$CFLAGS"
  ac_cppflags_save="$CPPFLAGS"
  CFLAGS="$CFLAGS $X_CFLAGS"
  CPPFLAGS="$CPPFLAGS $X_CFLAGS"
  AC_CHECK_HEADER(X11/X.h, HAVE_X="yes", HAVE_X="no")

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

dnl DirectDraw
translit(dnm, m, l) AM_CONDITIONAL(USE_DIRECTDRAW, true)
GST_CHECK_FEATURE(DIRECTDRAW, [DirectDraw plug-in], directdrawsink, [
  HAVE_DIRECTDRAW="no"
  save_LIBS="$LIBS"
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
  LIBS=$save_LIBS

  if test "x$HAVE_DIRECTDRAW" = "xyes";  then
    dnl this is much more than we want
    DIRECTDRAW_LIBS="-lddraw -lgdi32"
    AC_SUBST(DIRECTDRAW_CFLAGS)
    AC_SUBST(DIRECTDRAW_LIBS)
  fi
  AC_SUBST(HAVE_DIRECTDRAW)
])

dnl DirectSound
translit(dnm, m, l) AM_CONDITIONAL(USE_DIRECTSOUND, true)
GST_CHECK_FEATURE(DIRECTSOUND, [DirectSound plug-in], directsoundsink, [
  HAVE_DIRECTSOUND="no"
  save_LIBS="$LIBS"
  LIBS="$LIBS -ldsound -ldxerr9"
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
  LIBS=$save_LIBS

  if test "x$HAVE_DIRECTSOUND" = "xyes";  then
    dnl this is much more than we want
    DIRECTSOUND_LIBS="-ldsound -ldxerr9"
    AC_SUBST(DIRECTSOUND_CFLAGS)
    AC_SUBST(DIRECTSOUND_LIBS)
  fi
  AC_SUBST(HAVE_DIRECTSOUND)
])

dnl *** OS X video ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OSX_VIDEO, true)
HAVE_OSX_VIDEO="no"
GST_CHECK_FEATURE(OSX_VIDEO, [OSX video], osxvideosink, [
  AC_CHECK_HEADER(OpenGL/gl.h, HAVE_OSX_VIDEO="yes", HAVE_OSX_VIDEO="no")
])
dnl in case header OpenGL/gl.h is found on other platforms
case "$host" in
  *-*darwin*)
    dnl do nothing
    ;;
  *)
    HAVE_OSX_VIDEO="no"
    ;;
esac



dnl *** ext plug-ins ***
dnl keep this list sorted alphabetically !

if test "x$BUILD_EXTERNAL" = "xyes"; then

dnl *** alsa ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ALSA, true)
GST_CHECK_FEATURE(ALSA, [alsa plug-ins], gstalsa, [
  PKG_CHECK_MODULES(ALSA, alsa >= 0.9.1, [
    HAVE_ALSA="yes"
    AC_SUBST(ALSA_CFLAGS)
    AC_SUBST(ALSA_LIBS)
  ], [
    AM_PATH_ALSA(0.9.1, HAVE_ALSA="yes", HAVE_ALSA="no")
  ])
])

dnl *** amrwb ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AMRWB, true)
GST_CHECK_FEATURE(AMRWB, [amrwb code], amrwb, [
  if test -f "gst-libs/ext/amrwb/26204-600.zip"; then
    HAVE_AMRWB="yes"
  else
    AC_MSG_WARN(amrwb code zipfile not found. See README at ext/amrwb/)
    HAVE_AMRWB="no"
  fi
  AC_SUBST(HAVE_AMRWB)
])

dnl *** BZ2 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_BZ2, true)
GST_CHECK_FEATURE(BZ2, [bz2 library], bz2, [
  GST_CHECK_LIBHEADER(BZ2, bz2, BZ2_bzCompress, ,bzlib.h, BZ2_LIBS="-lbz2")
  AC_SUBST(BZ2_LIBS)
])

#dnl *** cairo ***
#translit(dnm, m, l) AM_CONDITIONAL(USE_CAIRO, true)
#GST_CHECK_FEATURE(CAIRO, [cairo plug-in], cairo, [
#  PKG_CHECK_MODULES(CAIRO, cairo >= 1.0 glitz-glx, HAVE_CAIRO=yes, [
#    HAVE_CAIRO=no
#    AC_MSG_RESULT(no)
#  ])
#  AC_SUBST(CAIRO_CFLAGS)
#  AC_SUBST(CAIRO_LIBS)
#])

dnl *** cdaudio ***
translit(dnm, m, l) AM_CONDITIONAL(USE_CDAUDIO, true)
GST_CHECK_FEATURE(CDAUDIO, [cdaudio], cdaudio, [
  GST_CHECK_CONFIGPROG(CDAUDIO, libcdaudio-config)
  AC_SUBST(CDAUDIO_CFLAGS)
  AC_SUBST(CDAUDIO_LIBS)
])

dnl **** DirectFB ****
translit(dnm, m, l) AM_CONDITIONAL(USE_DIRECTFB, true)
GST_CHECK_FEATURE(DIRECTFB, [directfb], dfbvideosink , [
  PKG_CHECK_MODULES(DIRECTFB, directfb >= 0.9.24, HAVE_DIRECTFB="yes", [
    HAVE_DIRECTFB="no"
    AC_MSG_RESULT(no)
  ])
])

dnl *** DTS ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DTS, true)
GST_CHECK_FEATURE(DTS, [dts library], dtsdec, [
  GST_CHECK_LIBHEADER(DTS, dts_pic, dts_init, -lm, dts.h,
    DTS_LIBS="-ldts_pic -lm")
  AC_SUBST(DTS_LIBS)
])

dnl *** DIVX ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DIVX, true)
GST_CHECK_FEATURE(DIVX, [divx plugins], divx, [
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

dnl **** Free AAC Encoder (FAAC) ****
translit(dnm, m, l) AM_CONDITIONAL(USE_FAAC, true)
GST_CHECK_FEATURE(FAAC, [AAC encoder plug-in], faac, [
  GST_CHECK_LIBHEADER(FAAC, faac, faacEncOpen, -lm, faac.h,
    FAAC_LIBS="-lfaac -lm")
  AS_SCRUB_INCLUDE(FAAC_CFLAGS)
  AC_SUBST(FAAC_LIBS)
])

dnl **** Free AAC Decoder (FAAD) ****
translit(dnm, m, l) AM_CONDITIONAL(USE_FAAD, true)
GST_CHECK_FEATURE(FAAD, [AAC decoder plug-in], faad, [
  HAVE_FAAD="yes"
  GST_CHECK_LIBHEADER(FAAD, faad, faacDecOpen, -lm, faad.h,
    FAAD_LIBS="-lfaad -lm", HAVE_FAAD="no")
  if test $HAVE_FAAD = "yes"; then
    AC_MSG_CHECKING([Checking for FAAD >= 2])
    AC_TRY_RUN([

#include <faad.h>
#if !defined(FAAD2_VERSION) || !defined(FAAD_FMT_DOUBLE)
#error Not faad2
#else
#include <string.h>

int main()
{
char version[9] = FAAD2_VERSION;
// a release candidate of 2.0 is not enought for us
if ( strstr( version, "2.0 RC" ) ) { return 1; }

return 0;
}

#endif
    ], [
      HAVE_FAAD="yes"
      AC_MSG_RESULT(yes)
    ], [
      HAVE_FAAD="no"
      AC_MSG_RESULT(no)
    ])
  fi
  AS_SCRUB_INCLUDE(FAAD_CFLAGS)
  AC_SUBST(FAAD_LIBS)
])

dnl *** gsm ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GSM, true)
GST_CHECK_FEATURE(GSM, [GSM library], gsmenc gsmdec, [
  GST_CHECK_LIBHEADER(GSM, gsm, gsm_create, , gsm.h, GSM_LIBS="-lgsm")
  if test $HAVE_GSM != "yes"; then
    GST_CHECK_LIBHEADER(GSM, gsm, gsm_create, , gsm/gsm.h, GSM_LIBS="-lgsm")
    if test $HAVE_GSM = "yes"; then 
      AC_DEFINE(GSM_HEADER_IN_SUBDIR, 1, [Define if GSM header in gsm/ subdir])
    fi
  fi
  AC_SUBST(GSM_LIBS)
])

dnl *** ivorbis ***
dnl AM_PATH_IVORBIS only takes two options
translit(dnm, m, l) AM_CONDITIONAL(USE_IVORBIS, true)
GST_CHECK_FEATURE(IVORBIS, [integer vorbis plug-in], ivorbisdec, [
  IVORBIS_LIBS=
  IVORBIS_CFLAGS=
  AC_CHECK_LIB(vorbisidec, vorbis_block_init,
    [IVORBIS_LIBS=-lvorbisidec
     HAVE_IVORBIS=yes],
    HAVE_IVORBIS=no)
  AC_SUBST(IVORBIS_LIBS)
  AC_SUBST(IVORBIS_CFLAGS)
])

dnl *** Jack ***
translit(dnm, m, l) AM_CONDITIONAL(USE_JACK, true)
GST_CHECK_FEATURE(JACK, Jack, jack, [
  PKG_CHECK_MODULES(JACK, jack >= 0.99.10, HAVE_JACK="yes", HAVE_JACK="no")
  AC_SUBST(JACK_CFLAGS)
  AC_SUBST(JACK_LIBS)
])

dnl *** ladspa ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LADSPA, true)
GST_CHECK_FEATURE(LADSPA, [ladspa], ladspa, [
  AC_CHECK_HEADER(ladspa.h, HAVE_LADSPA="yes", HAVE_LADSPA="no")
])

dnl *** libmms ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBMMS, true)
GST_CHECK_FEATURE(LIBMMS, [mms protocol library], libmms, [
  dnl check with pkg-config first
  PKG_CHECK_MODULES(LIBMMS, libmms >= 0.2, HAVE_LIBMMS="yes", [
    HAVE_LIBMMS="no"
    AC_MSG_RESULT(no)
  ])
])
AC_SUBST(LIBMMS_LIBS)

dnl *** mjpegtools version info ***
dnl some may prefer older version (given quirks above)
dnl hm, no version info seems available within mjpegtools headers
PKG_CHECK_EXISTS(mjpegtools >= 1.8.0 mjpegtools < 1.9.0, [
  AC_DEFINE(GST_MJPEGTOOLS_18x, 1, [mjpegtools >= 1.8.0 is used])
  have_mpjegtools_18x=yes
  ], [
  have_mpjegtools_18x=no])

dnl *** mpeg2enc ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MPEG2ENC, true)
GST_CHECK_FEATURE(MPEG2ENC, [mpeg2enc], mpeg2enc, [
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
          if test "x$have_mpjegtools_18x" = "xyes"; then
            AC_CHECK_HEADER([mpeg2syntaxcodes.h], [
                mpeg2enc_headers_ok=yes
              ], [
                mpeg2enc_headers_ok=no
              ])
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

dnl *** musepack ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MUSEPACK, true)
GST_CHECK_FEATURE(MUSEPACK, [musepackdec], musepack, [
  AC_LANG_CPLUSPLUS
  AC_CHECK_HEADER([mpcdec/mpcdec.h], [
    HAVE_MUSEPACK="yes"
    MUSEPACK_LIBS="-lmpcdec"
    AC_SUBST(MUSEPACK_LIBS)
    ], [HAVE_MUSEPACK="no"])
  AC_LANG_C
])

dnl *** musicbrainz ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MUSICBRAINZ, true)
GST_CHECK_FEATURE(MUSICBRAINZ, [musicbrainz tag generation], musicbrainz, [
  PKG_CHECK_MODULES(MUSICBRAINZ, libmusicbrainz >= 2.1.0, HAVE_MUSICBRAINZ="yes", HAVE_MUSICBRAINZ="no")
  AC_SUBST(MUSICBRAINZ_CFLAGS)
  AC_SUBST(MUSICBRAINZ_LIBS)
])

dnl *** MythTV ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MYTHTV, true)
GST_CHECK_FEATURE(MYTHTV, [MythTV client plugins], mythtvsrc, [
  PKG_CHECK_MODULES(GMYTH, gmyth-0.1 >= 0.1.0.3, HAVE_MYTHTV="yes", [
    HAVE_MYTHTV="no"
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(GMYTH_CFLAGS)
  AC_SUBST(GMYTH_LIBS)
])

dnl *** neon ***
translit(dnm, m, l) AM_CONDITIONAL(USE_NEON, true)
GST_CHECK_FEATURE(NEON, [neon http client plugins], neonhttpsrc, [
  PKG_CHECK_MODULES(NEON, neon >= 0.25.5  neon <= 0.26.99, HAVE_NEON="yes", [
    HAVE_NEON="no"
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(NEON_CFLAGS)
  AC_SUBST(NEON_LIBS)
])

dnl *** SDL ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SDL, true)
GST_CHECK_FEATURE(SDL, [SDL plug-in], sdlvideosink sdlaudiosink, [
    AM_PATH_SDL(, HAVE_SDL=yes, HAVE_SDL=no)
])

dnl *** soundtouch ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SOUNDTOUCH, true)
GST_CHECK_FEATURE(SOUNDTOUCH, [soundtouch plug-in], soundtouch, [
  PKG_CHECK_MODULES(SOUNDTOUCH, libSoundTouch, HAVE_SOUNDTOUCH=yes, [
    HAVE_SOUNDTOUCH=no
     AC_MSG_RESULT(no)])
  AC_SUBST(SOUNDTOUCH_CFLAGS)
  AC_SUBST(SOUNDTOUCH_LIBS)
  if test "x$HAVE_CXX" != "xyes"; then
    USE_SOUNDTOUCH=false
    AC_MSG_NOTICE([Not building soundtouch plugin: no C++ compiler found])
  fi
])

dnl *** spc ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SPC, true)
GST_CHECK_FEATURE(SPC, [spc decoder], spc, [
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

dnl *** swfdec ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SWFDEC, true)
GST_CHECK_FEATURE(SWFDEC, [swfdec plug-in], swfdec, [
  PKG_CHECK_MODULES(SWFDEC, swfdec-0.3 >= 0.3.6, HAVE_SWFDEC=yes, [
    HAVE_SWFDEC=no
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(SWFDEC_CFLAGS)
  AC_SUBST(SWFDEC_LIBS)
])

dnl *** theora ***
translit(dnm, m, l) AM_CONDITIONAL(USE_THEORADEC, true)
GST_CHECK_FEATURE(THEORADEC, [ogg theora codec], theoraexpdec, [
  PKG_CHECK_MODULES(THEORADEC, theoradec, HAVE_THEORADEC="yes", [
    HAVE_THEORADEC="no"
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(THEORADEC_LIBS)
  AC_SUBST(THEORADEC_CFLAGS)
])

dnl *** XVID ***
translit(dnm, m, l) AM_CONDITIONAL(USE_XVID, true)
GST_CHECK_FEATURE(XVID, [xvid plugins], xvid, [
  HAVE_XVID=no
  AC_CHECK_HEADER(xvid.h, [
    OLD_LIBS="$LIBS"
    LIBS="-lm"
    AC_CHECK_LIB(xvidcore, xvid_encore, [
      AC_CHECK_LIB(xvidcore, xvid_decore, [
        AC_CHECK_LIB(xvidcore, xvid_global, [
          AC_MSG_CHECKING([for uptodate XviD API version])
          AC_TRY_RUN([
#include <xvid.h>
#if XVID_API_MAJOR(XVID_API) != 4
#error "Incompatible XviD API version"
#endif
int main () { return 0; }
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

dnl *** wavpack ***
translit(dnm, m, l) AM_CONDITIONAL(USE_WAVPACK, true)
GST_CHECK_FEATURE(WAVPACK, [wavpack plug-in], wavpack, [
  PKG_CHECK_MODULES(WAVPACK, wavpack >= 4.40.0, HAVE_WAVPACK=yes, [
    PKG_CHECK_MODULES(WAVPACK, wavpack >= 4.20, [
      HAVE_WAVPACK=yes
      AC_DEFINE(WAVPACK_OLD_API, 1, [old wavpack API])
    ],[
      HAVE_WAVPACK=no
      AC_MSG_RESULT(no)
    ])
  ])
  AC_SUBST(WAVPACK_CFLAGS)
  AC_SUBST(WAVPACK_LIBS)
])

dnl *** dvb ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DVB, true)
GST_CHECK_FEATURE(DVB, [DVB Source], dvb, [
  AC_MSG_CHECKING([Checking for up to date dvb installation])
  AC_CHECK_HEADER(linux/dvb/frontend.h, [HAVE_DVB="yes"], [HAVE_DVB="no"])
])

dnl *** qtdemux prefers to have zlib ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ZLIB, true)
GST_CHECK_FEATURE(ZLIB, [zlib support for qtdemux],, [
  GST_CHECK_LIBHEADER(ZLIB,
    z, uncompress,, zlib.h, [
    HAVE_ZLIB="yes"
    ZLIB_LIBS="-lz"
    AC_SUBST(ZLIB_LIBS)
  ])
])

else

dnl not building plugins with external dependencies,
dnl but we still need to set the conditionals

AM_CONDITIONAL(USE_OPENGL, false)
AM_CONDITIONAL(USE_X, false)
AM_CONDITIONAL(USE_ALSA, false)
AM_CONDITIONAL(USE_AMRWB, false)
AM_CONDITIONAL(USE_BZ2, false)
AM_CONDITIONAL(USE_DIRECTFB, false)
AM_CONDITIONAL(USE_DTS, false)
AM_CONDITIONAL(USE_DIVX, false)
AM_CONDITIONAL(USE_FAAC, false)
AM_CONDITIONAL(USE_FAAD, false)
AM_CONDITIONAL(USE_GSM, false)
AM_CONDITIONAL(USE_IVORBIS, false)
AM_CONDITIONAL(USE_LADSPA, false)
AM_CONDITIONAL(USE_LIBMMS, false)
AM_CONDITIONAL(USE_MUSEPACK, false)
AM_CONDITIONAL(USE_MUSICBRAINZ, false)
AM_CONDITIONAL(USE_MYTHTV, false)
AM_CONDITIONAL(USE_NEON, false)
AM_CONDITIONAL(USE_SDL, false)
AM_CONDITIONAL(USE_SOUNDTOUCH, false)
AM_CONDITIONAL(USE_SPC, false)
AM_CONDITIONAL(USE_SWFDEC, false)
AM_CONDITIONAL(USE_THEORADEC, false)
AM_CONDITIONAL(USE_XVID, false)
AM_CONDITIONAL(USE_WAVPACK, false)
AM_CONDITIONAL(USE_DVB, false)
AM_CONDITIONAL(USE_ZLIB, false)

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

DEPRECATED_CFLAGS="-DGST_DISABLE_DEPRECATED"
AC_SUBST(DEPRECATED_CFLAGS)

dnl every flag in GST_OPTION_CFLAGS can be overridden at make time
GST_OPTION_CFLAGS="\$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
AC_SUBST(GST_OPTION_CFLAGS)

dnl FIXME: do we want to rename to GST_ALL_* ?
dnl prefer internal headers to already installed ones
dnl also add builddir include for enumtypes and marshal
dnl add GST_OPTION_CFLAGS, but overridable
GST_CFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs $GST_CFLAGS \$(GST_OPTION_CFLAGS)"
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_ALL_LDFLAGS="-no-undefined"
AC_SUBST(GST_ALL_LDFLAGS)

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_desc\$\$' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl *** output files ***

dnl po/Makefile.in

AC_CONFIG_FILES(
Makefile
gst-plugins-bad.spec
gst/Makefile
gst/cdxaparse/Makefile
gst/deinterlace/Makefile
gst/filter/Makefile
gst/freeze/Makefile
gst/h264parse/Makefile
gst/interleave/Makefile
gst/librfb/Makefile
gst/modplug/Makefile
gst/modplug/libmodplug/Makefile
gst/multifile/Makefile
gst/mve/Makefile
gst/nsf/Makefile
gst/nuvdemux/Makefile
gst/replaygain/Makefile
gst/spectrum/Makefile
gst/speed/Makefile
gst/qtdemux/Makefile
gst/tta/Makefile
gst/videocrop/Makefile
gst/videoparse/Makefile
gst/xingheader/Makefile
gst/real/Makefile
gst/y4m/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/ext/Makefile
gst-libs/ext/amrwb/Makefile
sys/Makefile
sys/glsink/Makefile
sys/dvb/Makefile
sys/directdraw/Makefile
sys/directsound/Makefile
sys/osxvideo/Makefile
examples/Makefile
examples/directfb/Makefile
ext/amrwb/Makefile
ext/alsaspdif/Makefile
ext/bz2/Makefile
ext/cdaudio/Makefile
ext/directfb/Makefile
ext/divx/Makefile
ext/dts/Makefile
ext/faac/Makefile
ext/faad/Makefile
ext/gsm/Makefile
ext/ivorbis/Makefile
ext/jack/Makefile
ext/ladspa/Makefile
ext/libmms/Makefile
ext/Makefile
ext/mpeg2enc/Makefile
ext/musepack/Makefile
ext/musicbrainz/Makefile
ext/mythtv/Makefile
ext/neon/Makefile
ext/sdl/Makefile
ext/soundtouch/Makefile
ext/spc/Makefile
ext/swfdec/Makefile
ext/theora/Makefile
ext/wavpack/Makefile
ext/xvid/Makefile
po/Makefile.in
docs/Makefile
docs/plugins/Makefile
docs/version.entities
common/Makefile
common/m4/Makefile
m4/Makefile
tests/Makefile
tests/check/Makefile
tests/icles/Makefile
)
AC_OUTPUT

GST_OUTPUT_PLUGINS
