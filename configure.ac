AC_PREREQ(2.52)

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT(GStreamer Bad Plug-ins, 0.10.5.1,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gst-plugins-bad)

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE

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

AM_PROG_LIBTOOL

dnl *** required versions of GStreamer stuff ***
dnl *** remove rtpmanager/equalizer stuff below when this is updated
GST_REQ=0.10.14.1
GSTPB_REQ=0.10.14.1

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl set up gettext
dnl the version check needs to stay here because autopoint greps for it
AM_GNU_GETTEXT_VERSION([0.11.5])
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

AG_GST_ARG_EXAMPLES

dnl these are all the gst plug-ins, compilable without additional libs
GST_PLUGINS_ALL="\
  app \
  bayer \
  cdxaparse \
  deinterlace \
  dvdspu \
  equalizer \
  festival \
  filter \
  flv \
  freeze \
  h264parse \
  interleave \
  librfb \
  modplug \
  mpegtsparse \
  mpeg4videoparse \
  mpegvideoparse \
  multifile \
  mve \
  nsf \
  nuvdemux \
  real \
  replaygain \
  rtpmanager \
  sdp \
  spectrum \
  speexresample \
  speed \
  stereo \
  switch \
  tta \
  videoparse \
  videosignal \
  vmnc \
  xingheader \
  y4m \
  "

AC_SUBST(GST_PLUGINS_ALL)

AG_GST_ARG_WITH_PLUGINS

AG_GST_ARG_ENABLE_EXTERNAL

AG_GST_ARG_ENABLE_EXPERIMENTAL

USE_GIO=$BUILD_EXPERIMENTAL

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
AG_GST_PLUGIN_DOCS([1.3],[2.1])

dnl *** checks for libraries ***

dnl check for libm, for sin() etc.
AC_CHECK_LIBM
AC_SUBST(LIBM)

dnl *** checks for header files ***

AC_CHECK_HEADERS([unistd.h], HAVE_UNISTD_H=yes)
AM_CONDITIONAL(HAVE_UNISTD_H, test "x$HAVE_UNISTD_H" = "xyes")

if test "x$HAVE_UNISTD_H" != "xyes"; then
  GST_PLUGINS_SELECTED=`echo $GST_PLUGINS_SELECTED | $SED -e s/festival//`
fi

dnl used by ext/dts
AX_CREATE_STDINT_H

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***

dnl *** checks for dependency libraries ***

dnl GLib is required
AG_GST_GLIB_CHECK([2.6])

dnl liboil is required
PKG_CHECK_MODULES(LIBOIL, liboil-0.3 >= 0.3.8, HAVE_LIBOIL=yes, HAVE_LIBOIL=no)
if test "x${HAVE_LIBOIL}" != xyes ; then
  AC_ERROR([liboil-0.3.8 or later is required])
fi

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_MAJORMINOR, [$GST_REQ])
AG_GST_CHECK_GST_BASE($GST_MAJORMINOR, [$GST_REQ])
AG_GST_CHECK_GST_GDP($GST_MAJORMINOR, [$GST_REQ])
AG_GST_CHECK_GST_CHECK($GST_MAJORMINOR, [$GST_REQ], no)
AG_GST_CHECK_GST_CONTROLLER($GST_MAJORMINOR, [$GST_REQ])
AG_GST_CHECK_GST_PLUGINS_BASE($GST_MAJORMINOR, [$GSTPB_REQ], no)
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
AG_GST_SET_PLUGINDIR

dnl define an ERROR_CFLAGS Makefile variable
AG_GST_SET_ERROR_CFLAGS($GST_CVS)

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT($GST_CVS)

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
AG_GST_CHECK_X
save_CPPFLAGS="$CPPFLAGS"
save_LIBS="$LIBS"
CPPFLAGS="$CPPFLAGS $X_CFLAGS"
LIBS="$LIBS $X_LIBS"
AG_GST_CHECK_FEATURE(OPENGL, [Open GL], glsink, [
  AG_GST_CHECK_LIBHEADER(GL, GL, glTexImage2D,,
                      GL/gl.h, HAVE_OPENGL="yes", HAVE_OPENGL="no")
])
CPPFLAGS="$save_CPPFLAGS"
LIBS="$save_LIBS"

dnl check for Video CD
translit(dnm, m, l) AM_CONDITIONAL(USE_VCD, true)
AG_GST_CHECK_FEATURE(VCD, [Video CD], vcdsrc, [
  AC_CHECK_HEADER(linux/cdrom.h, HAVE_VCD="yes", HAVE_VCD="no")
])

dnl Check for X11
translit(dnm, m, l) AM_CONDITIONAL(USE_X, true)
AG_GST_CHECK_FEATURE(X, [X libraries and plugins],
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

dnl *** amrwb ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AMRWB, true)
AG_GST_CHECK_FEATURE(AMRWB, [amrwb library], amrwb, [
  AG_GST_CHECK_LIBHEADER(AMRWB, amrwb,
                        GP3D_IF_decode, ,
                        amrwb/dec.h,
                        AMRWB_LIBS="-lamrwb"
                        AC_SUBST(AMRWB_LIBS))
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


dnl *** dc1394  ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DC1394, true)
AG_GST_CHECK_FEATURE(DC1394, [libdc1394], dc1394, [
   PKG_CHECK_MODULES(LIBDC1394, libdc1394-2 >= 2.0.0-rc5, HAVE_DC1394="yes", [
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

dnl *** DTS ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DTS, true)
AG_GST_CHECK_FEATURE(DTS, [dts library], dtsdec, [
  AG_GST_CHECK_LIBHEADER(DTS, dts_pic, dts_init, -lm, dts.h,
    DTS_LIBS="-ldts_pic -lm")
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
  AC_CHECK_HEADER(exempi-2.0/exempi/xmp.h, 
    [
    AC_CHECK_LIB(exempi, xmp_init, 
      [
      HAVE_XMP="yes"
      XMP_CFLAGS="-I/usr/local/include/exempi-2.0/exempi/"
      XMP_LIBS="-lexempi"
      ], HAVE_XMP="no")
    ], HAVE_XMP="no")
  if test x$HAVE_EXIF = xyes; then
    METADATA_CFLAGS="-DHAVE_EXIF $EXIF_CFLAGS $METADATA_CFLAGS"
    METADATA_LIBS="$EXIF_LIBS $METADATA_LIBS"
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
  fi
  dnl metadata plugin will parse whole chunks anyway
  HAVE_METADATA="yes"
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
  AG_GST_CHECK_LIBHEADER(FAAD, faad, faacDecOpen, -lm, faad.h,
    FAAD_LIBS="-lfaad -lm", HAVE_FAAD="no")
  if test $HAVE_FAAD = "no"; then
    AG_GST_CHECK_LIBHEADER(FAAD, faad, NeAACDecOpen, -lm, neaacdec.h,
      [ FAAD_LIBS="-lfaad -lm"
	  	AC_DEFINE(FAAD_IS_NEAAC, 1, [Define if AAC is using new api prefix])
	  ],
	  HAVE_FAAD="no",)
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
    ], [
      HAVE_FAAD="no"
      AC_MSG_RESULT(no)
    ])
  fi
  AS_SCRUB_INCLUDE(FAAD_CFLAGS)
  AC_SUBST(FAAD_LIBS)
  AC_SUBST(FAAD_IS_NEAAC)
])

dnl *** libgio ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GIO, true)
AG_GST_CHECK_FEATURE(GIO, [GIO library], gio, [
  PKG_CHECK_MODULES(GIO, gio-2.0 >= 0.1.2, HAVE_GIO="yes", [
    HAVE_GIO="no"
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(GIO_CFLAGS)
  AC_SUBST(GIO_LIBS)
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

dnl *** ladspa ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LADSPA, true)
AG_GST_CHECK_FEATURE(LADSPA, [ladspa], ladspa, [
  AC_CHECK_HEADER(ladspa.h, HAVE_LADSPA="yes", HAVE_LADSPA="no")
])

dnl *** libmms ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBMMS, true)
AG_GST_CHECK_FEATURE(LIBMMS, [mms protocol library], libmms, [
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
AG_GST_CHECK_FEATURE(MUSEPACK, [musepackdec], musepack, [
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
AG_GST_CHECK_FEATURE(MUSICBRAINZ, [musicbrainz tag generation], musicbrainz, [
  PKG_CHECK_MODULES(MUSICBRAINZ, libmusicbrainz >= 2.1.0, HAVE_MUSICBRAINZ="yes", HAVE_MUSICBRAINZ="no")
  AC_SUBST(MUSICBRAINZ_CFLAGS)
  AC_SUBST(MUSICBRAINZ_LIBS)
])

dnl *** MythTV ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MYTHTV, true)
AG_GST_CHECK_FEATURE(MYTHTV, [MythTV client plugins], mythtvsrc, [
  PKG_CHECK_MODULES(GMYTH, gmyth >= 0.4 gmyth <= 0.4.99, HAVE_MYTHTV="yes", [
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
  PKG_CHECK_MODULES(NEON, neon >= 0.26.0  neon <= 0.27.99, HAVE_NEON="yes", [
    HAVE_NEON="no"
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(NEON_CFLAGS)
  AC_SUBST(NEON_LIBS)
])

dnl *** soup ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SOUP, true)
AG_GST_CHECK_FEATURE(SOUP, [soup http client plugins], souphttpsrc, [
  PKG_CHECK_MODULES(SOUP, libsoup-2.2, HAVE_SOUP="yes", [
    HAVE_SOUP="no"
    AC_MSG_RESULT(no)
  ])
  AC_SUBST(SOUP_CFLAGS)
  AC_SUBST(SOUP_LIBS)
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

dnl *** x264 (MPEG-4 part 10/h.264/AVC encoder) ***
translit(dnm, m, l) AM_CONDITIONAL(USE_X264, true)
AG_GST_CHECK_FEATURE(X264, [x264 plug-in], x264, [
  AG_GST_CHECK_LIBHEADER(X264, x264, x264_nal_encode, -lm, x264.h,
      X264_LIBS="$LDFLAGS -lx264 -lm"
      X264_CFLAGS="$CFLAGS"
      AC_SUBST(X264_LIBS)
      AC_SUBST(X264_CFLAGS))
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

else

dnl not building plugins with external dependencies,
dnl but we still need to set the conditionals

AM_CONDITIONAL(USE_OPENGL, false)
AM_CONDITIONAL(USE_VCD, false)
AM_CONDITIONAL(USE_X, false)
AM_CONDITIONAL(USE_ALSA, false)
AM_CONDITIONAL(USE_AMRWB, false)
AM_CONDITIONAL(USE_BZ2, false)
AM_CONDITIONAL(USE_CDAUDIO, false)
AM_CONDITIONAL(USE_DIRECTFB, false)
AM_CONDITIONAL(USE_DTS, false)
AM_CONDITIONAL(USE_DIVX, false)
AM_CONDITIONAL(USE_FAAC, false)
AM_CONDITIONAL(USE_FAAD, false)
AM_CONDITIONAL(USE_GIO, false)
AM_CONDITIONAL(USE_GSM, false)
AM_CONDITIONAL(USE_IVORBIS, false)
AM_CONDITIONAL(USE_JACK, false)
AM_CONDITIONAL(USE_LADSPA, false)
AM_CONDITIONAL(USE_LIBMMS, false)
AM_CONDITIONAL(USE_MPEG2ENC, false)
AM_CONDITIONAL(USE_MUSEPACK, false)
AM_CONDITIONAL(USE_MUSICBRAINZ, false)
AM_CONDITIONAL(USE_MYTHTV, false)
AM_CONDITIONAL(USE_NAS, false)
AM_CONDITIONAL(USE_NEON, false)
AM_CONDITIONAL(USE_SOUP, false)
AM_CONDITIONAL(USE_SDL, false)
AM_CONDITIONAL(USE_SNDFILE, false)
AM_CONDITIONAL(USE_SOUNDTOUCH, false)
AM_CONDITIONAL(USE_SPC, false)
AM_CONDITIONAL(USE_SWFDEC, false)
AM_CONDITIONAL(USE_THEORADEC, false)
AM_CONDITIONAL(USE_TIMIDITY, false)
AM_CONDITIONAL(USE_X264, false)
AM_CONDITIONAL(USE_XVID, false)
AM_CONDITIONAL(USE_WILDMIDI, false)
AM_CONDITIONAL(USE_DVB, false)

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
gst/app/Makefile
gst/bayer/Makefile
gst/cdxaparse/Makefile
gst/deinterlace/Makefile
gst/dvdspu/Makefile
gst/equalizer/Makefile
gst/festival/Makefile
gst/filter/Makefile
gst/flv/Makefile
gst/freeze/Makefile
gst/h264parse/Makefile
gst/interleave/Makefile
gst/librfb/Makefile
gst/modplug/Makefile
gst/modplug/libmodplug/Makefile
gst/mpegtsparse/Makefile
gst/mpeg4videoparse/Makefile
gst/mpegvideoparse/Makefile
gst/multifile/Makefile
gst/mve/Makefile
gst/nsf/Makefile
gst/nuvdemux/Makefile
gst/replaygain/Makefile
gst/rtpmanager/Makefile
gst/sdp/Makefile
gst/spectrum/Makefile
gst/speed/Makefile
gst/speexresample/Makefile
gst/stereo/Makefile
gst/switch/Makefile
gst/tta/Makefile
gst/videoparse/Makefile
gst/videosignal/Makefile
gst/vmnc/Makefile
gst/xingheader/Makefile
gst/real/Makefile
gst/y4m/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/app/Makefile
sys/Makefile
sys/glsink/Makefile
sys/dvb/Makefile
sys/vcd/Makefile
examples/Makefile
examples/app/Makefile
examples/directfb/Makefile
examples/switch/Makefile
ext/amrwb/Makefile
ext/alsaspdif/Makefile
ext/bz2/Makefile
ext/cdaudio/Makefile
ext/dc1394/Makefile
ext/directfb/Makefile
ext/divx/Makefile
ext/dts/Makefile
ext/metadata/Makefile
ext/faac/Makefile
ext/faad/Makefile
ext/gio/Makefile
ext/gsm/Makefile
ext/ivorbis/Makefile
ext/jack/Makefile
ext/ladspa/Makefile
ext/libmms/Makefile
ext/Makefile
ext/nas/Makefile
ext/mpeg2enc/Makefile
ext/musepack/Makefile
ext/musicbrainz/Makefile
ext/mythtv/Makefile
ext/neon/Makefile
ext/soup/Makefile
ext/sdl/Makefile
ext/sndfile/Makefile
ext/soundtouch/Makefile
ext/spc/Makefile
ext/swfdec/Makefile
ext/theora/Makefile
ext/timidity/Makefile
ext/x264/Makefile
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
win32/common/config.h
)
AC_OUTPUT

AG_GST_OUTPUT_PLUGINS
