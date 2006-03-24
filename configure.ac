AC_PREREQ(2.52)

dnl initialize autoconf
dnl we don't specify PACKAGE and VERSION because AS_VERSION puts it together
AC_INIT

dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AS_VERSION(gst-plugins-base, GST_PLUGINS_BASE_VERSION, 0, 10, 5, 1,
    GST_CVS="no", GST_CVS="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/audiotestsrc/gstaudiotestsrc.c])

dnl define the output header for config
AM_CONFIG_HEADER([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl sets host_* variables
AC_CANONICAL_HOST

dnl initialize automake
dnl FIXME: this is deprecated use, we should move PACKAGE and VERSION to
dnl AC_INIT, but then we need to parse the version to pass to AS_VERSION
AM_INIT_AUTOMAKE($PACKAGE, $VERSION)

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$GST_PLUGINS_BASE_VERSION_MAJOR.$GST_PLUGINS_BASE_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
dnl sets GST_LT_LDFLAGS
AS_LIBTOOL(GST, 2, 0, 2)
AS_LIBTOOL_TAGS
AM_PROG_LIBTOOL

dnl *** required versions of GStreamer stuff ***
GST_REQ=0.10.4.1

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl set up gettext
dnl the version check needs to stay here because autopoint greps for it
AM_GNU_GETTEXT_VERSION([0.11.5])
AM_GNU_GETTEXT([external])
GST_GETTEXT([gst-plugins-base-$GST_MAJORMINOR])

dnl *** check for arguments to configure ***

GST_ARG_DEBUG
GST_ARG_PROFILING
GST_ARG_VALGRIND
GST_ARG_GCOV

GST_ARG_EXAMPLES

GST_ARG_WITH_PKG_CONFIG_PATH
GST_ARG_WITH_PACKAGE_NAME([GStreamer Base Plug-ins])
GST_ARG_WITH_PACKAGE_ORIGIN

dnl these are all the gst plug-ins, compilable without additional libs
GST_PLUGINS_ALL="\
	adder \
	audioconvert \
	audiorate \
	audioresample \
	audiotestsrc \
	ffmpegcolorspace \
	playback \
	typefind \
	videotestsrc \
	videorate \
	videoscale \
	volume \
	"
AC_SUBST(GST_PLUGINS_ALL)

GST_PLUGINS_SELECTED=""

AC_ARG_WITH(plugins,
    AC_HELP_STRING([--with-plugins],
      [comma-separated list of plug-ins to compile]),
    [for i in `echo $withval | tr , ' '`; do
        if echo $GST_PLUGINS_ALL | grep $i > /dev/null
        then
            GST_PLUGINS_SELECTED="$GST_PLUGINS_SELECTED $i"
        else
            echo "plug-in $i not recognized, ignoring..."
        fi
    done],
    [GST_PLUGINS_SELECTED=$GST_PLUGINS_ALL])

AC_SUBST(GST_PLUGINS_SELECTED)

dnl ext plug-ins; plug-ins that have external dependencies
GST_CHECK_FEATURE(EXTERNAL, [enable building of plug-ins with external deps],,
  [HAVE_EXTERNAL=yes], enabled,
  [
    AC_MSG_NOTICE(building external plug-ins)
    BUILD_EXTERNAL="yes"
  ],[
    AC_MSG_NOTICE(all plug-ins with external dependencies will not be built)
    BUILD_EXTERNAL="no"
  ])
AM_CONDITIONAL(BUILD_EXTERNAL, test "x$BUILD_EXTERNAL" = "xyes")

dnl *** checks for platform ***

dnl * hardware/architecture *

dnl common/m4/gst-arch.m4
dnl check CPU type
GST_ARCH

dnl *** checks for programs ***

dnl find a compiler
AC_PROG_CC

dnl determine c++ compiler
AC_PROG_CXX
dnl determine if c++ is available on this system
AC_CHECK_PROG(HAVE_CXX, $CXX, yes, no)

dnl determine c++ preprocessor
dnl FIXME: do we need this ?
AC_PROG_CXXCPP

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for documentation tools
GTK_DOC_CHECK([1.3])
AS_PATH_PYTHON([2.1])

dnl *** checks for libraries ***

dnl For interactive UNIX (a Sun thing)
dnl FIXME: this adds -lcposix to LIBS, but I doubt we use LIBS
AC_ISC_POSIX

dnl *** checks for header files ***

dnl check if we have ANSI C header files
AC_HEADER_STDC

dnl used in gst/ffmpegcolorspace/mem.c
dnl FIXME: could be fixed by redefining av_malloc and av_free to GLib's
AC_CHECK_HEADERS([malloc.h])

dnl used in gst/subparse/gstsubparse.c
AC_CHECK_HEADERS([regex.h], HAVE_REGEX_H="yes", HAVE_REGEX_H="no")
AM_CONDITIONAL(HAVE_REGEX_H, test "x$HAVE_REGEX_H" = "xyes")

dnl used in gst/tcp
AC_CHECK_HEADERS([sys/socket.h], 
  HAVE_SYS_SOCKET_H="yes", HAVE_SYS_SOCKET_H="no")
AM_CONDITIONAL(HAVE_SYS_SOCKET_H, test "x$HAVE_SYS_SOCKET_H" = "xyes")

dnl ffmpegcolorspace includes _stdint.h
dnl also, Windows does not have long long
AX_CREATE_STDINT_H

dnl *** checks for types/defines ***

dnl Check for FIONREAD ioctl declaration
dnl used in gst/tcp
GST_CHECK_FIONREAD

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl make sure we can use "inline" from C code
AC_C_INLINE

dnl *** checks for library functions ***

dnl Check for fast float to int casting as defined in C99
dnl used in gst-libs/gst/floatcast/floatcast.h
AC_C99_FUNC_LRINT
AC_C99_FUNC_LRINTF

dnl Check for a way to display the function name in debug output
GST_CHECK_FUNCTION

dnl *** checks for dependancy libraries ***

dnl GLib is required
GST_GLIB_CHECK([2.6])

dnl liboil is required
PKG_CHECK_MODULES(LIBOIL, liboil-0.3 >= 0.3.6, HAVE_LIBOIL=yes, HAVE_LIBOIL=no)
if test "x${HAVE_LIBOIL}" != xyes ; then
  AC_ERROR([liboil-0.3.6 or later is required])
fi

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
GST_CHECK_GST($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_BASE($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_GDP($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_CONTROLLER($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_CHECK($GST_MAJORMINOR, [$GST_REQ], no)

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

dnl *** set variables based on configure arguments ***

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

dnl used in examples
GST_DEFAULT_ELEMENTS

dnl *** sys plug-ins ***

echo
AC_MSG_NOTICE([Checking libraries for plugins in sys/])
echo

dnl *** X11 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_X, true)
GST_CHECK_FEATURE(X, [X libraries and plugins],
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
  
dnl *** XVideo ***
dnl Look for the PIC library first, Debian requires it.
dnl Check debian-devel archives for gory details.
dnl 20020110:
dnl At the moment XFree86 doesn't distribute shared libXv due
dnl to unstable API.  On many platforms you CAN NOT link a shared
dnl lib to a static non-PIC lib.  This is what the xvideo GStreamer
dnl plug-in wants to do.  So Debian distributes a PIC compiled
dnl version of the static lib for plug-ins to link to when it is
dnl inappropriate to link the main application to libXv directly.
dnl FIXME: add check if this platform can support linking to a
dnl        non-PIC libXv, if not then don not use Xv.
dnl FIXME: perhaps warn user if they have a shared libXv since
dnl        this is an error until XFree86 starts shipping one
   
dnl Check for Xv extension
translit(dnm, m, l) AM_CONDITIONAL(USE_XVIDEO, true)
GST_CHECK_FEATURE(XVIDEO, [X11 XVideo extensions],
                  [xvimagesink], [
  if test x$HAVE_X = xyes; then
    AC_CHECK_LIB(Xv_pic, XvQueryExtension,
		 HAVE_XVIDEO="yes", HAVE_XVIDEO="no",
		 $X_LIBS -lXext)

    if test x$HAVE_XVIDEO = xyes; then
      XVIDEO_LIBS="-lXv_pic -lXext"
      AC_SUBST(XVIDEO_LIBS)
    else
      dnl try again using something else if we didn't find it first
      if test x$HAVE_XVIDEO = xno; then
        AC_CHECK_LIB(Xv, XvQueryExtension,
		   HAVE_XVIDEO="yes", HAVE_XVIDEO="no",
		   $X_LIBS -lXext)

        if test x$HAVE_XVIDEO = xyes; then
          XVIDEO_LIBS="-lXv -lXext"
          AC_SUBST(XVIDEO_LIBS)
        fi
      fi
    fi
  fi
])

dnl check for X Shm
translit(dnm, m, l) AM_CONDITIONAL(USE_XSHM, true)
GST_CHECK_FEATURE(XSHM, [X Shared Memory extension], xshm, [
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
] )

dnl v4l/v4l2 checks have been moved down because they require X

dnl *** Video 4 Linux ***
dnl for information about the header/define, see sys/v4l/gstv4lelement.h
dnl renamed to GST_V4L in accordance with V4L2 below
translit(dnm, m, l) AM_CONDITIONAL(USE_GST_V4L, true)
GST_CHECK_FEATURE(GST_V4L, [Video 4 Linux], v4lsrc v4lmjpegsrc v4lmjpegsink, [
  # first check X
  HAVE_GST_V4L="no"
  if test "$HAVE_X" = "yes"
  then
    AC_CHECK_DECL(VID_TYPE_MPEG_ENCODER, HAVE_GST_V4L="yes", HAVE_GST_V4L="no", [
#include <sys/types.h>
#define _LINUX_TIME_H
#define __user
#include <linux/videodev.h>
    ])
  fi
])

dnl *** ext plug-ins ***
dnl keep this list sorted alphabetically !

if test "x$BUILD_EXTERNAL" = "xyes"; then

echo
AC_MSG_NOTICE([Checking libraries for plugins in ext/])
echo

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

dnl *** CDParanoia ***
translit(dnm, m, l) AM_CONDITIONAL(USE_CDPARANOIA, true)
GST_CHECK_FEATURE(CDPARANOIA, [CDParanoia], cdparanoia, [
  GST_CHECK_LIBHEADER(CDPARANOIA, cdda_interface, 
                      cdda_open, -lm, 
                      cdda_interface.h, 
                      CDPARANOIA_LIBS="-lcdda_interface -lcdda_paranoia"
                      HEADER_DIR="no"
                      FOUND_CDPARANOIA="yes")
  if test "x$FOUND_CDPARANOIA" != "xyes";
  then
    GST_CHECK_LIBHEADER(CDPARANOIA, cdda_interface, 
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
  AC_SUBST(CDPARANOIA_LIBS)
])
dnl FIXME : add second check somehow if that is necessary
dnl AC_CHECK_LIB(cdda_paranoia, paranoia_init, : , HAVE_CDPARANOIA=no, -lcdda_interface )
dnl AC_CHECK_HEADER(cdda_paranoia.h, :, HAVE_CDPARANOIA=no)
GST_CHECK_FEATURE(EXTERNAL, [enable building of plug-ins with external deps],,
  [HAVE_EXTERNAL=yes], enabled,
  [
    AC_MSG_NOTICE(building external plug-ins)
    BUILD_EXTERNAL="yes"
  ],[
    AC_MSG_NOTICE(all plug-ins with external dependencies will not be built)
    BUILD_EXTERNAL="no"
  ])


dnl *** Gnome VFS ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GNOME_VFS, true)
GST_CHECK_FEATURE(GNOME_VFS, [Gnome VFS], gnomevfssrc, [
  PKG_CHECK_MODULES(GNOME_VFS, gnome-vfs-2.0, HAVE_GNOME_VFS="yes", HAVE_GNOME_VFS="no")
  AC_SUBST(GNOME_VFS_CFLAGS)
  AC_SUBST(GNOME_VFS_LIBS)
])

dnl *** libvisual ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBVISUAL, true)
GST_CHECK_FEATURE(LIBVISUAL, [libvisual visualization plugins], libvisual, [
  PKG_CHECK_MODULES(LIBVISUAL, libvisual = 0.2.0, HAVE_LIBVISUAL="yes", HAVE_LIBVISUAL="no")
  AC_SUBST(LIBVISUAL_CFLAGS)
  AC_SUBST(LIBVISUAL_LIBS)
])

dnl *** ogg ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OGG, true)
GST_CHECK_FEATURE(OGG, [ogg de/encoder], oggdemux oggmux, [
  PKG_CHECK_MODULES(OGG, ogg >= 1.0, [
    HAVE_OGG="yes"
    AC_SUBST(OGG_CFLAGS)
    AC_SUBST(OGG_LIBS)
  ], [
    XIPH_PATH_OGG(HAVE_OGG="yes", HAVE_OGG="no")
    AS_SCRUB_INCLUDE(OGG_CFLAGS)
  ])
])

dnl *** pango ***
translit(dnm, m, l) AM_CONDITIONAL(USE_PANGO, true)
GST_CHECK_FEATURE(PANGO, [pango], pango, [
  PKG_CHECK_MODULES(PANGO, pango pangoft2,
      HAVE_PANGO="yes", HAVE_PANGO="no")
  AC_SUBST(PANGO_CFLAGS)
  AC_SUBST(PANGO_LIBS)
])

dnl *** theora ***
translit(dnm, m, l) AM_CONDITIONAL(USE_THEORA, true)
GST_CHECK_FEATURE(THEORA, [ogg theora codec], theoradec theoraenc, [
  PKG_CHECK_MODULES(THEORA, theora, [
    HAVE_THEORA="yes"
    AC_SUBST(THEORA_LIBS)
    AC_SUBST(THEORA_CFLAGS)
  ], [
    GST_CHECK_LIBHEADER(THEORA, theora, theora_version_string, "-logg", theora/theora.h, THEORA_LIBS="-ltheora -logg")
    AC_SUBST(THEORA_LIBS)
  ])
])

dnl *** vorbis ***
dnl AM_PATH_VORBIS only takes two options
translit(dnm, m, l) AM_CONDITIONAL(USE_VORBIS, true)
GST_CHECK_FEATURE(VORBIS, [vorbis plug-in], vorbisenc vorbisdec, [
  PKG_CHECK_MODULES(VORBIS, vorbis >= 1.0 vorbisenc >= 1.0, [
    HAVE_VORBIS="yes"
  ], [
    XIPH_PATH_VORBIS(HAVE_VORBIS="yes", HAVE_VORBIS="no")
    AS_SCRUB_INCLUDE(VORBIS_CFLAGS)
  ])
])
if test "x$HAVE_VORBIS" = "xyes"; then
  ac_cflags_save="$CFLAGS"
  dnl FIXME: does this work on non-gcc? -- Company
  dnl FIXME: no, it doesn't.  Why is this here in the first place ? -- thomasvs
  CFLAGS="-Wall -Werror"
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

dnl our libraries need to be versioned correctly
AC_SUBST(GST_LT_LDFLAGS)

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

dnl GST_LIB_LDFLAGS
dnl linker flags shared by all libraries
dnl LDFLAGS modifier defining exported symbols from built libraries
GST_LIB_LDFLAGS="-export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*"
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
gst/audioconvert/Makefile
gst/audiorate/Makefile
gst/audioresample/Makefile
gst/audiotestsrc/Makefile
gst/ffmpegcolorspace/Makefile
gst/playback/Makefile
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
ext/libvisual/Makefile
ext/ogg/Makefile
ext/pango/Makefile
ext/theora/Makefile
ext/vorbis/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/audio/Makefile
gst-libs/gst/cdda/Makefile
gst-libs/gst/floatcast/Makefile
gst-libs/gst/interfaces/Makefile
gst-libs/gst/netbuffer/Makefile
gst-libs/gst/riff/Makefile
gst-libs/gst/rtp/Makefile
gst-libs/gst/tag/Makefile
gst-libs/gst/video/Makefile
tools/Makefile
win32/common/config.h
pkgconfig/Makefile
pkgconfig/gstreamer-plugins-base.pc
pkgconfig/gstreamer-plugins-base-uninstalled.pc
tests/Makefile
tests/check/Makefile
tests/examples/Makefile
tests/examples/seek/Makefile
tests/examples/volume/Makefile
tests/icles/Makefile
docs/Makefile
docs/libs/Makefile
docs/plugins/Makefile
docs/version.entities
po/Makefile.in
common/Makefile
common/m4/Makefile
m4/Makefile
)
AC_OUTPUT

echo "configure: *** Core plug-ins, always built:"
( for i in $GST_PLUGINS_ALL; do echo -e '\t'$i; done ) | sort
echo
echo -n "configure: *** Plug-ins relying on libraries that will be built:"
echo -e "$GST_PLUGINS_YES" | sort
echo
echo -n "configure: *** Plug-ins relying on libraries that will NOT be built:"
echo -e "$GST_PLUGINS_NO" | sort
echo
if test "x$BUILD_EXTERNAL" = "xno"; then
  echo "configure: *** No external plug-ins will be built"
fi
