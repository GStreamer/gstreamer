dnl autoconf configuration file for gst-plugins 
AC_INIT
AC_CANONICAL_TARGET

dnl We disable static building for development, for time savings
dnl this goes before AS_LIBTOOL to appease autoconf
dnl *NOTE*: dnl this line before release, so release does static too
AM_DISABLE_STATIC

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AS_VERSION(gst-plugins-bad, GST_PLUGINS_BAD_VERSION, 0, 9, 4, 1,
    GST_CVS="no", GST_CVS="yes")

AM_INIT_AUTOMAKE($PACKAGE,$VERSION)

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$GST_PLUGINS_BAD_VERSION_MAJOR.$GST_PLUGINS_BAD_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_MAJORMINOR=0.9
AC_SUBST(GST_MAJORMINOR)

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
dnl for 0.8.3 release, gst_play_get_all_by_interface was added, so update
AS_LIBTOOL(GST_PLUGINS, 1, 0, 1)
AS_LIBTOOL_TAGS([CXX])
AM_PROG_LIBTOOL

AC_CONFIG_SRCDIR([gst/qtdemux/qtdemux.c])
AM_CONFIG_HEADER(config.h)

dnl Add parameters for aclocal
dnl (This must come after AM_INIT_AUTOMAKE, since it modifies ACLOCAL)
ACLOCAL_FLAGS="-I m4 -I common/m4"
AC_SUBST(ACLOCAL_AMFLAGS, $ACLOCAL_FLAGS)

AC_PROG_CC
AM_PROG_CC_STDC
AM_PROG_AS
AS="${CC}"
AS_PROG_OBJC

dnl the gettext stuff needed
AM_GNU_GETTEXT_VERSION(0.11.5)
AM_GNU_GETTEXT([external])
                                                                            
GETTEXT_PACKAGE=gst-plugins-bad-$GST_MAJORMINOR
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], "$GETTEXT_PACKAGE",
                  [gettext package name])

dnl define LOCALEDIR in config.h
AS_AC_EXPAND(LOCALEDIR, $datadir/locale)
AC_DEFINE_UNQUOTED([LOCALEDIR], "$LOCALEDIR",
                   [gettext locale dir])

dnl decide on error flags
AS_COMPILER_FLAG(-Wall, GST_WALL="yes", GST_WALL="no")

if test "x$GST_WALL" = "xyes"; then
   GST_ERROR="$GST_ERROR -Wall"

   if test "x$GST_CVS" = "xyes"; then
     AS_COMPILER_FLAG(-Werror,GST_ERROR="$GST_ERROR -Werror",GST_ERROR="$GST_ERROR")
   fi
fi

dnl determine c++ compiler
AC_PROG_CXX
dnl determine if c++ is available on this system
AC_CHECK_PROG(HAVE_CXX, $CXX, yes, no)
dnl determine c++ preprocessor
AC_PROG_CXXCPP
AC_ISC_POSIX

AC_HEADER_STDC([])
AC_C_INLINE
AX_CREATE_STDINT_H

dnl Check for malloc.h
AC_CHECK_HEADER(malloc.h,[
  AC_DEFINE(HAVE_MALLOC_H, 1, [whether malloc.h available])
])

dnl Check for a way to display the function name in debug output
GST_CHECK_FUNCTION()

dnl define correct errorlevel for debugging messages. We want to have GST_ERROR
dnl messages printed when running cvs builds
if test "x$GST_CVS" = "xyes"; then
  AC_DEFINE(GST_LEVEL_DEFAULT, GST_LEVEL_ERROR, [Default errorlevel to use])
fi

dnl Check for FIONREAD ioctl declaration :
GST_CHECK_FIONREAD()

DEFAULT_AUDIOSINK="autoaudiosink"
DEFAULT_VIDEOSINK="autovideosink"
DEFAULT_AUDIOSRC="alsasrc"
DEFAULT_VIDEOSRC="v4lsrc"
DEFAULT_VISUALIZER="goom"
case "$host" in 
  *-sun-* | *pc-solaris* )
    DEFAULT_AUDIOSRC="sunaudiosrc"
    ;;
  *-darwin* )
    DEFAULT_AUDIOSRC="osxaudiosrc"
    ;;
esac

AC_SUBST(DEFAULT_AUDIOSINK)
AC_DEFINE_UNQUOTED(DEFAULT_AUDIOSINK,"$DEFAULT_AUDIOSINK",[Default audio sink])
AC_SUBST(DEFAULT_AUDIOSRC)
AC_DEFINE_UNQUOTED(DEFAULT_AUDIOSRC,"$DEFAULT_AUDIOSRC",[Default audio source])
AC_SUBST(DEFAULT_VIDEOSINK)
AC_DEFINE_UNQUOTED(DEFAULT_VIDEOSINK,"$DEFAULT_VIDEOSINK",[Default video sink])
AC_SUBST(DEFAULT_VIDEOSRC)
AC_DEFINE_UNQUOTED(DEFAULT_VIDEOSRC,"$DEFAULT_VIDEOSRC",[Default video source])
AC_SUBST(DEFAULT_VISUALIZER)
AC_DEFINE_UNQUOTED(DEFAULT_VISUALIZER,"$DEFAULT_VISUALIZER",[Default visualizer])

dnl ############################################
dnl # Super Duper options for plug-in building #
dnl ############################################

dnl ext plug-ins; plug-ins that have external dependencies
GST_CHECK_FEATURE(EXTERNAL, [enable building of plug-ins with external deps],,
[HAVE_EXTERNAL=yes],enabled,
[
  AC_MSG_WARN(building external plug-ins)
  BUILD_EXTERNAL="yes"
],[
  AC_MSG_WARN(all plug-ins with external dependencies will not be built)
  BUILD_EXTERNAL="no"
])
# make BUILD_EXTERNAL available to Makefile.am
AM_CONDITIONAL(BUILD_EXTERNAL, test "x$BUILD_EXTERNAL" = "xyes")

dnl experimental plug-ins; stuff that hasn't had the dust settle yet
dnl read 'builds, but might not work'UTO
GST_CHECK_FEATURE(EXPERIMENTAL, [enable building of experimental plug-ins],,
[HAVE_EXPERIMENTAL=yes],disabled,
[
  AC_MSG_WARN(building experimental plug-ins)
  USE_TARKIN="yes"
],[
  AC_MSG_NOTICE(not building experimental plug-ins)
  USE_TARKIN="no"
])

dnl broken plug-ins; stuff that doesn't seem to build at the moment
GST_CHECK_FEATURE(BROKEN, [enable building of broken plug-ins],,
HAVE_BROKEN=yes,disabled,
[  
  AC_MSG_WARN([building broken plug-ins -- no bug reports on these, only patches :)])
],[
  AC_MSG_NOTICE([not building broken plug-ins])
])

dnl ##############################
dnl # Do automated configuration #
dnl ##############################

dnl Check for tools:
dnl ================

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE()

dnl modify pkg-config path
AC_ARG_WITH(pkg-config-path, 
   AC_HELP_STRING([--with-pkg-config-path],[colon-separated list of pkg-config(1) dirs]),
   [export PKG_CONFIG_PATH=${withval}])

GST_DOCBOOK_CHECK()
dnl check architecture
GST_ARCH()

dnl check for gconftool-2

dnl this macro defines an am conditional, so it needs to be run always
AM_GCONF_SOURCE_2

translit(dnm, m, l) AM_CONDITIONAL(USE_GCONFTOOL, true)
GST_CHECK_FEATURE(GCONFTOOL, [GConf schemas], , [
  AC_PATH_PROG(GCONFTOOL, gconftool-2, no)
  if test x$GCONFTOOL = xno; then
    AC_MSG_WARN(Not installing GConf schemas)
    HAVE_GCONFTOOL="no"
  else
    HAVE_GCONFTOOL="yes"
  fi
  AC_SUBST(HAVE_GCONFTOOL)
])

dnl check for GConf libraries
translit(dnm, m, l) AM_CONDITIONAL(USE_GCONF, true)
GST_CHECK_FEATURE(GCONF, [GConf libraries], , [
  PKG_CHECK_MODULES(GCONF, gconf-2.0, HAVE_GCONF="yes", HAVE_GCONF="no")
  AC_SUBST(GCONF_CFLAGS)
  AC_SUBST(GCONF_LIBS)
])

dnl check for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
GST_REQ=0.9.0.1
PKG_CHECK_MODULES(GST, gstreamer-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST="yes", HAVE_GST="no")

if test "x$HAVE_GST" = "xno"; then
  AC_MSG_ERROR(no GStreamer found)
fi

GST_TOOLS_DIR=`pkg-config --variable=toolsdir gstreamer-$GST_MAJORMINOR`
if test -z $GST_TOOLS_DIR; then
  AC_MSG_ERROR([no tools dir defined in GStreamer pkg-config file; core upgrade needed.])
fi
AC_SUBST(GST_TOOLS_DIR)

dnl check for gstreamer-base; uninstalled is selected preferentially
PKG_CHECK_MODULES(GST_BASE, gstreamer-base-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_BASE="yes", HAVE_GST_BASE="no")

if test "x$HAVE_GST_BASE" = "xno"; then
  AC_MSG_ERROR(no GStreamer Base Libs found)
fi

AC_SUBST(GST_BASE_LIBS)
AC_SUBST(GST_BASE_CFLAGS)

dnl check for gstreamer-plugins-base; uinstalled is selected preferentially
PKG_CHECK_MODULES(GST_PLUGINS_BASE,
  gstreamer-plugins-base-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_PLUGINS_BASE="yes", HAVE_GST_PLUGINS_BASE="no")

if test "x$HAVE_GST_PLUGINS_BASE" = "xno"; then
  AC_MSG_ERROR(no GStreamer Base Plugins development files found)
fi

AC_SUBST(GST_PLUGINS_BASE_LIBS)
AC_SUBST(GST_PLUGINS_BASE_CFLAGS)

dnl Determine endianness
AC_C_BIGENDIAN

dnl Check for fast float to int casting as defined in C99
AC_C99_FUNC_LRINT()
AC_C99_FUNC_LRINTF()

dnl Check for essential libraries first:
dnl ====================================

GST_GLIB_CHECK([2.6.0])

dnl Check for additional libraries that we might use:
dnl =================================================

# we set the defaults always to make sure we have non-empty variables
# for the Makefile

PKG_CHECK_MODULES(LIBOIL, liboil-0.3 >= 0.3.0, HAVE_LIBOIL=yes, HAVE_LIBOIL=no)
AC_SUBST(LIBOIL_CFLAGS)
AC_SUBST(LIBOIL_LIBS)
if test "x${HAVE_LIBOIL}" = xyes ; then
  #AC_DEFINE_UNQUOTED(HAVE_LIBOIL, 1, [Define if liboil is being used])
  true
fi

dnl ===========================================================================
dnl ============================= gst plug-ins ================================
dnl ===========================================================================

plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"
AC_SUBST(plugindir)

GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '[_]*(gst_|Gst|GST_).*' $GST_LIBS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl these are all the gst plug-ins, compilable without additional libs
GST_PLUGINS_ALL="\
		speed   \
		qtdemux \
		tta     \
                "

dnl see if we can build C++ plug-ins
if test "x$HAVE_CXX" = "xyes"; then
  GST_PLUGINS_ALL="$GST_PLUGINS_ALL"
else
  AC_MSG_WARN([Not compiling plug-ins requiring C++ compiler])
fi

AC_SUBST(GST_PLUGINS_ALL)

GST_PLUGINS_SELECTED=""

AC_ARG_WITH(plugins,
    AC_HELP_STRING([--with-plugins],[comma-separated list of plug-ins to compile]),
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

dnl ==========================================================================
dnl ============================= sys plug-ins ================================
dnl ==========================================================================


dnl ###########################
dnl # Configure external libs #
dnl ###########################

dnl **** DirectFB ****
translit(dnm, m, l) AM_CONDITIONAL(USE_DIRECTFB, true)
GST_CHECK_FEATURE(DIRECTFB, [directfb], dfbvideosink , [
  PKG_CHECK_MODULES(DIRECTFB, directfb, [
    HAVE_DIRECTFB="yes"
    AC_SUBST(DIRECTFB_CFLAGS)
    AC_SUBST(DIRECTFB_LIBS)
  ], [
    HAVE_DIRECTFB="no"
  ])
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

dnl Check for X11

dnl now try to find the HEADER
AC_CHECK_HEADER(X11/Xlib.h, HAVE_X="yes", HAVE_X="no")

AC_PATH_XTRA

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


dnl *** SDL ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SDL, true)
GST_CHECK_FEATURE(SDL, [SDL plug-in], sdlvideosink, [
  dnl sdlvideosink depends on the xoverlay interface, which depends on X
  if test x$HAVE_X = xyes; then
    AM_PATH_SDL(, HAVE_SDL=yes, HAVE_SDL=no)
  fi
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

dnl also add builddir include for enumtypes and marshal
GST_CFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs $GST_CFLAGS $GST_ERROR"

AC_SUBST(GST_LIBS)
AC_SUBST(GST_CFLAGS)

dnl ############################
dnl # Set up some more defines #
dnl ############################

dnl set license and copyright notice
AC_DEFINE(GST_LICENSE, "LGPL", [GStreamer license])

dnl package name in plugins
AC_ARG_WITH(package-name,
AC_HELP_STRING([--with-package-name],[specify package name to use in plugins]),
[case "${withval}" in
  yes) AC_MSG_ERROR(bad value ${withval} for --with-package-name) ;;
  no) AC_MSG_ERROR(bad value ${withval} for --with-package-name) ;;
  *) GST_PACKAGE="${withval}" ;;
esac], 
[
dnl default value
if test "x$GST_CVS" = "xyes"
then
  dnl nano >= 1
  GST_PACKAGE="GStreamer CVS/prerelease"
else
  GST_PACKAGE="GStreamer source release"
fi
]
)
AC_MSG_NOTICE(Using $GST_PACKAGE as package name)
AC_DEFINE_UNQUOTED(GST_PACKAGE, "$GST_PACKAGE", [package name in plugins])

dnl package origin URL
AC_ARG_WITH(package-origin,
AC_HELP_STRING([--with-package-origin],[specify package origin URL to use in plugins]),
[case "${withval}" in
  yes) AC_MSG_ERROR(bad value ${withval} for --with-package-origin) ;;
  no) AC_MSG_ERROR(bad value ${withval} for --with-package-origin) ;;
  *) GST_ORIGIN="${withval}" ;;
esac], 
[GST_ORIGIN="http://gstreamer.freedesktop.org/"]) dnl Default value
AC_MSG_NOTICE(Using $GST_ORIGIN as package origin)
AC_DEFINE_UNQUOTED(GST_ORIGIN, "$GST_ORIGIN", [package origin])

dnl #########################
dnl # Make the output files #
dnl #########################
dnl po/Makefile.in

AC_CONFIG_FILES(
Makefile
gst-plugins-bad.spec
gst/Makefile
gst/speed/Makefile
gst/qtdemux/Makefile
gst/tta/Makefile
sys/Makefile
ext/Makefile
ext/directfb/Makefile
ext/faac/Makefile
ext/faad/Makefile
ext/gsm/Makefile
ext/sdl/Makefile
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
