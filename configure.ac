AC_PREREQ(2.52)

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT(GStreamer Bad Plug-ins, 0.10.2.1,
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
GST_REQ=0.10.3
GSTPB_REQ=0.10.3

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
  freeze \
  speed \
  qtdemux \
  xingheader \
  tta \
  "

dnl see if we can build C++ plug-ins
if test "x$HAVE_CXX" = "xyes"; then
  GST_PLUGINS_ALL="$GST_PLUGINS_ALL \
    modplug \
    "
else
  AC_MSG_WARN([Not compiling plug-ins requiring C++ compiler])
fi

AC_SUBST(GST_PLUGINS_ALL)

GST_PLUGINS_SELECTED=""

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
GST_CHECK_GST_CHECK($GST_MAJORMINOR, [$GST_REQ], no)

dnl FIXME: get rid of this by making sure gstreamer-check brings it in
dnl check for "check", unit testing library/header
AM_PATH_CHECK(0.9.2, HAVE_CHECK=yes, HAVE_CHECK=no)
AM_CONDITIONAL(HAVE_CHECK, test "x$HAVE_CHECK" = "xyes")

GST_CHECK_GST_PLUGINS_BASE($GST_MAJORMINOR, [$GSTPB_REQ], no)
GSTPB_PLUGINS_DIR=`$PKG_CONFIG gstreamer-plugins-base-$GST_MAJORMINOR --variable pluginsdir`
AC_SUBST(GSTPB_PLUGINS_DIR)
AC_MSG_NOTICE(Using GStreamer Base Plugins in $GSTPB_PLUGINS_DIR)

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

dnl *** sys plug-ins ***

dnl OpenGL
translit(dnm, m, l) AM_CONDITIONAL(USE_OPENGL, true)
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

    dnl check for Xfixes
    PKG_CHECK_MODULES(XFIXES, xfixes, HAVE_XFIXES="yes", HAVE_XFIXES="no")
    if test "x$HAVE_XFIXES" = "xyes"
    then
        XFIXES_CFLAGS="-DHAVE_XFIXES $XFIXES_CFLAGS"
    fi
    AC_SUBST(XFIXES_LIBS)
    AC_SUBST(XFIXES_CFLAGS)

    dnl check for Xdamage
    PKG_CHECK_MODULES(XDAMAGE, xdamage, HAVE_XDAMAGE="yes", HAVE_XDAMAGE="no")
    if test "x$HAVE_XDAMAGE" = "xyes"
    then
        XDAMAGE_CFLAGS="-DHAVE_XDAMAGE $XDAMAGE_CFLAGS"
    fi
    AC_SUBST(XDAMAGE_LIBS)
    AC_SUBST(XDAMAGE_CFLAGS)
  fi
  AC_SUBST(HAVE_X)
  CFLAGS="$ac_cflags_save"
  CPPFLAGS="$ac_cppflags_save"
])

dnl FIXME: this should be rolled into the test above, it's just an additional
dnl feature of the ximagesrc plug-in
dnl check for X Shm
translit(dnm, m, l) AM_CONDITIONAL(USE_XSHM, true)
GST_CHECK_FEATURE(XSHM, [X Shared Memory extension], [], [
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
], ,[
  AC_SUBST(HAVE_XSHM)
  AC_SUBST(XSHM_LIBS)
])

dnl *** Video 4 Linux 2 ***
dnl for information about the header/define, see sys/v4l2/gstv4l2element.h
dnl renamed to GST_V4L2 because of some conflict with kernel headers
translit(dnm, m, l) AM_CONDITIONAL(USE_GST_V4L2, true)
GST_CHECK_FEATURE(GST_V4L2, [Video 4 Linux 2], v4l2src, [
  AC_MSG_CHECKING([Checking for uptodate v4l2 installation])
  AC_TRY_COMPILE([
#include <sys/types.h>
#include <linux/types.h>
#define _LINUX_TIME_H
#define __user
#include <linux/videodev2.h>
#if defined(V4L2_MAJOR_VERSION) || defined(V4L2_MINOR_VERSION)
#error too early v4l2 version or no v4l2 at all
#endif
    ], [
return 0;
    ], [
      HAVE_GST_V4L2="yes"
      AC_MSG_RESULT(yes)
    ], [
      HAVE_GST_V4L2="no"
      AC_MSG_RESULT(no)
      AC_CHECK_HEADER(linux/videodev2.h,
      [
        AC_MSG_WARN([video4linux2 headers were found, but they're old.])
        AC_MSG_WARN([Please update v4l2 to compile the v4l2 plugins])
      ], [
        AC_MSG_WARN([video4linux2 was not found])
      ])
    ])

  if [ test x$HAVE_GST_V4L2 = xyes ]; then
    dnl check for missing v4l2_buffer declaration (see #135919)
    MISSING_DECL=0
    AC_MSG_CHECKING(struct v4l2_buffer declaration)
    AC_TRY_COMPILE([
#include <sys/types.h>
#include <linux/types.h>
#define _LINUX_TIME_H
#define __user
#include <linux/videodev2.h>
    ],[
struct v4l2_buffer buf;
buf.index = 0;
return 0;
    ], [ AC_MSG_RESULT(yes) ], [ MISSING_DECL=1 && AC_MSG_RESULT(no) ])
    if [ test x$MISSING_DECL = x1 ]; then
      AC_DEFINE(GST_V4L2_MISSING_BUFDECL, 1, [struct v4l2_buffer missing])
    fi

    dnl check for XOverlay libraries
    GST_CHECK_XV
  fi
])
AM_CONDITIONAL(USE_XVIDEO, test "x$HAVE_XVIDEO" = "xyes")

dnl *** ext plug-ins ***
dnl keep this list sorted alphabetically !

if test "x$BUILD_EXTERNAL" = "xyes"; then

dnl *** amrwb ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AMRWB, true)
GST_CHECK_FEATURE(AMRWB, [amrwb code], amrwb, [
  if test -f "ext/amrwb/amrwb-code/readme.txt"; then
    HAVE_AMRWB="yes"
  else
    AC_MSG_WARN(Not amrwb code found. See README at ext/amrwb/)
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
#  PKG_CHECK_MODULES(CAIRO, cairo >= 1.0 glitz-glx, HAVE_CAIRO=yes, HAVE_CAIRO=no)
#  AC_SUBST(CAIRO_CFLAGS)
#  AC_SUBST(CAIRO_LIBS)
#])

dnl **** DirectFB ****
translit(dnm, m, l) AM_CONDITIONAL(USE_DIRECTFB, true)
GST_CHECK_FEATURE(DIRECTFB, [directfb], dfbvideosink , [
  PKG_CHECK_MODULES(DIRECTFB, directfb >= 0.9.24, [
    HAVE_DIRECTFB="yes"
    AC_SUBST(DIRECTFB_CFLAGS)
    AC_SUBST(DIRECTFB_LIBS)
  ], [
    HAVE_DIRECTFB="no"
  ])
])

dnl *** DTS ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DTS, true)
GST_CHECK_FEATURE(DTS, [dts library], dtsdec, [
  GST_CHECK_LIBHEADER(DTS, dts_pic, dts_init, -lm, dts.h, DTS_LIBS="-ldts_pic -lm")
  AC_SUBST(DTS_LIBS)
])

dnl *** DIVX ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DIVX, true)
GST_CHECK_FEATURE(DIVX, [divx plugins], divx, [
  HAVE_DIVX=yes
  AC_CHECK_HEADER(encore2.h, ,
                  [ AC_MSG_WARN([Divx4linux encore headers not found]) &&
                    HAVE_DIVX=no ] )
  if [ test x$HAVE_DIVX = xyes ]; then
    AC_MSG_CHECKING([Checking for valid divx4linux encore version])
    AC_TRY_COMPILE([
#include <encore2.h>
#if ENCORE_VERSION != 20021024
#error Wrong version of divx encore libraries
#endif
    ], [
return 0;
    ], [ HAVE_DIVX=yes && AC_MSG_RESULT(yes)],
       [ HAVE_DIVX=no  && AC_MSG_RESULT(no) &&
         AC_MSG_WARN([Wrong version of divx4linux installed]) ])
  fi
  if [ test x$HAVE_DIVX = xyes ]; then
    AC_CHECK_HEADER(decore.h, ,
                    [ AC_MSG_WARN([Divx4linux decoder headers not found]) &&
                      HAVE_DIVX=no ] )
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
    ], [ HAVE_DIVX=yes && AC_MSG_RESULT(yes)],
       [ HAVE_DIVX=no  && AC_MSG_RESULT(no) &&
         AC_MSG_WARN([Wrong version of divx4linux installed]) ])
  fi
  LIBS="-lm"
  if test x$HAVE_DIVX = xyes; then
    AC_CHECK_LIB(divxencore, encore, ,
                 [ AC_MSG_WARN([Divx4linux encore libs not found]) &&
                   HAVE_DIVX=no ] )
  fi
  if test x$HAVE_DIVX = xyes; then
    AC_CHECK_LIB(divxdecore, decore, ,
                 [ AC_MSG_WARN([Divx4linux decore libs not found]) &&
                   HAVE_DIVX=no ] )
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
  GST_CHECK_LIBHEADER(FAAC, faac, faacEncOpen, -lm, faac.h, FAAC_LIBS="-lfaac -lm")
  AS_SCRUB_INCLUDE(FAAC_CFLAGS)
  AC_SUBST(FAAC_LIBS)
])

dnl **** Free AAC Decoder (FAAD) ****
translit(dnm, m, l) AM_CONDITIONAL(USE_FAAD, true)
GST_CHECK_FEATURE(FAAD, [AAC decoder plug-in], faad, [
  HAVE_FAAD="yes"
  GST_CHECK_LIBHEADER(FAAD, faad, faacDecOpen, -lm, faad.h, FAAD_LIBS="-lfaad -lm", HAVE_FAAD="no")
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
    ],
       [ HAVE_FAAD="yes" && AC_MSG_RESULT(yes)],
       [ HAVE_FAAD="no"  && AC_MSG_RESULT(no)])
  fi;
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

dnl *** libmms ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBMMS, true)
GST_CHECK_FEATURE(LIBMMS, [mms protocol library], libmms, [
  dnl check with pkg-config first
  PKG_CHECK_MODULES(LIBMMS, libmms >= 0.2, HAVE_LIBMMS="yes", HAVE_LIBMMS="no")
])
AC_SUBST(LIBMMS_LIBS)

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

dnl *** neon ***
translit(dnm, m, l) AM_CONDITIONAL(USE_NEON, true)
GST_CHECK_FEATURE(NEON, [neon http client plugins], neonhttpsrc, [
  PKG_CHECK_MODULES(NEON, neon >= 0.25.5, HAVE_NEON="yes", HAVE_NEON="no")
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
  PKG_CHECK_MODULES(SOUNDTOUCH, libSoundTouch, HAVE_SOUNDTOUCH=yes, HAVE_SOUNDTOUCH=no)
  AC_SUBST(SOUNDTOUCH_CFLAGS)
  AC_SUBST(SOUNDTOUCH_LIBS)
  if test "x$HAVE_CXX" != "xyes"; then
    USE_SOUNDTOUCH=false
    AC_MSG_NOTICE([Not building soundtouch plugin: no C++ compiler found])
  fi
])

dnl *** swfdec ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SWFDEC, true)
GST_CHECK_FEATURE(SWFDEC, [swfdec plug-in], swfdec, [
  PKG_CHECK_MODULES(SWFDEC, swfdec-0.3 >= 0.3.6, HAVE_SWFDEC=yes, HAVE_SWFDEC=no)
  AC_SUBST(SWFDEC_CFLAGS)
  AC_SUBST(SWFDEC_LIBS)
])


dnl *** taglib ***
translit(dnm, m, l) AM_CONDITIONAL(USE_TAGLIB, true)
GST_CHECK_FEATURE(TAGLIB, [taglib ID3v2 tag writer], taglib, [
  PKG_CHECK_MODULES(TAGLIB, taglib >= 1.4, HAVE_TAGLIB="yes", HAVE_TAGLIB="no")
  AC_SUBST(TAGLIB_CFLAGS)
  AC_SUBST(TAGLIB_LIBS)
  if test "x$HAVE_CXX" != "xyes"; then
    USE_TAGLIB=false
    AC_MSG_NOTICE([Not building taglib plugin: no C++ compiler found])
  fi
])

dnl *** theora ***
translit(dnm, m, l) AM_CONDITIONAL(USE_THEORADEC, true)
GST_CHECK_FEATURE(THEORADEC, [ogg theora codec], theoraexpdec, [
  PKG_CHECK_MODULES(THEORADEC, theoradec, HAVE_THEORADEC="yes", HAVE_THEORADEC="no")
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
  PKG_CHECK_MODULES(WAVPACK, wavpack >= 4.2, HAVE_WAVPACK=yes, HAVE_WAVPACK=no)
  AC_SUBST(WAVPACK_CFLAGS)
  AC_SUBST(WAVPACK_LIBS)
])

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
gst/freeze/Makefile
gst/modplug/Makefile
gst/modplug/libmodplug/Makefile
gst/speed/Makefile
gst/qtdemux/Makefile
gst/tta/Makefile
gst/xingheader/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
sys/Makefile
sys/glsink/Makefile
sys/ximagesrc/Makefile
sys/v4l2/Makefile
examples/Makefile
examples/directfb/Makefile
ext/amrwb/amrwb-code/Makefile
ext/amrwb/Makefile
ext/bz2/Makefile
ext/directfb/Makefile
ext/divx/Makefile
ext/dts/Makefile
ext/faac/Makefile
ext/faad/Makefile
ext/gsm/Makefile
ext/ivorbis/Makefile
ext/libmms/Makefile
ext/Makefile
ext/musepack/Makefile
ext/neon/Makefile
ext/sdl/Makefile
ext/soundtouch/Makefile
ext/swfdec/Makefile
ext/taglib/Makefile
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
