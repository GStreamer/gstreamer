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
AS_VERSION(gst-plugins, GST_PLUGINS_VERSION, 0, 9, 0, 1, GST_CVS="no", GST_CVS="yes")

AM_INIT_AUTOMAKE($PACKAGE,$VERSION)

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$GST_PLUGINS_VERSION_MAJOR.$GST_PLUGINS_VERSION_MINOR
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
AM_PROG_LIBTOOL

dnl FIXME take something else ?
AC_CONFIG_SRCDIR([gst/law/alaw.c])
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
dnl AM_GNU_GETTEXT_VERSION(0.11.5)
dnl AM_GNU_GETTEXT([external])
                                                                                
dnl GETTEXT_PACKAGE=gst-plugins-$GST_MAJORMINOR
dnl AC_SUBST(GETTEXT_PACKAGE)
dnl AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], "$GETTEXT_PACKAGE",
dnl                   [gettext package name])

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

DEFAULT_AUDIOSINK="osssink"
DEFAULT_VIDEOSINK="xvimagesink"
DEFAULT_AUDIOSRC="osssrc"
DEFAULT_VIDEOSRC="v4lsrc"
DEFAULT_VISUALIZER="goom"
case "$host" in 
  *-sun-* | *pc-solaris* )
    DEFAULT_AUDIOSINK="sunaudiosink"
    DEFAULT_VIDEOSINK="ximagesink"
    DEFAULT_AUDIOSRC="sunaudiosrc"
    ;;
  *-darwin* )
    DEFAULT_AUDIOSINK="osxaudiosink"
    DEFAULT_AUDIOSRC="osxaudiosrc"
    DEFAULT_VIDEOSINK="osxvideosink"
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

GST_DOC()
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

dnl check for gstreamer-control; uninstalled is selected preferentially
PKG_CHECK_MODULES(GST_CONTROL, gstreamer-control-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_CONTROL="yes", HAVE_GST_CONTROL="no")

if test "x$HAVE_GST_CONTROL" = "xno"; then
  AC_MSG_ERROR(no GStreamer Control Libs found)
fi

AC_SUBST(GST_CONTROL_LIBS)

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

GST_GLIB2_CHECK()

dnl Check for additional libraries that we might use:
dnl =================================================

# we set the defaults always to make sure we have non-empty variables
# for the Makefile

PKG_CHECK_MODULES(LIBOIL, liboil-0.2, HAVE_LIBOIL=yes, HAVE_LIBOIL=no)
#PKG_CHECK_MODULES(LIBOIL, liboil-0.3 >= 0.3.0, HAVE_LIBOIL=yes, HAVE_LIBOIL=no)
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
		videofilter \
		alpha \
		avi \
		effectv \
		goom \
		law \
		level \
		realmedia \
		rtp	\
		rtsp	\
		smpte	\
		udp     \
		videobox \
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

dnl *** OSS audio *** (Linux, *BSD)
dnl translit(dnm, m, l) AM_CONDITIONAL(USE_OSS, true)
dnl GST_CHECK_FEATURE(OSS, [OSS audio], osssrc osssink, [
dnl  HAVE_OSS="yes"
  dnl Linux and newer BSD versions :
dnl  AC_CHECK_HEADER(sys/soundcard.h, [
dnl    AC_DEFINE(HAVE_OSS_INCLUDE_IN_SYS,, [Define if OSS includes are in /sys/])
dnl    ] , [
    dnl Some old BSD versions :
dnl    AC_CHECK_HEADER(soundcard.h, [
dnl      AC_DEFINE(HAVE_OSS_INCLUDE_IN_ROOT,, [Define if OSS includes are in /])
dnl      ], [
      dnl Some old BSD versions :
dnl      AC_CHECK_HEADER(machine/soundcard.h, [
dnl        AC_DEFINE(HAVE_OSS_INCLUDE_IN_MACHINE,, [Define if OSS includes are in /machine/])
dnl        ], [
dnl        HAVE_OSS="no"
dnl      ])
dnl    ])
dnl  ])
dnl ])

dnl ###########################
dnl # Configure external libs #
dnl ###########################

dnl *** aalib ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AALIB, true)
GST_CHECK_FEATURE(AALIB, [aasink plug-in], aasink, [
  AM_PATH_AALIB(, HAVE_AALIB=yes, HAVE_AALIB=no)
  AS_SCRUB_INCLUDE(AALIB_CFLAGS)
])

dnl *** libcaca ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBCACA, true)
GST_CHECK_FEATURE(LIBCACA, [libcaca], libcaca, [
  GST_CHECK_CONFIGPROG(LIBCACA, caca-config)
  AC_SUBST(LIBCACA_CFLAGS)
  AC_SUBST(LIBCACA_LIBS)
])

dnl *** shout2 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SHOUT2, true)
GST_CHECK_FEATURE(SHOUT2, [shout2 plug-in], shout2send, [
  PKG_CHECK_MODULES(SHOUT2, shout >= 2.0, [
    HAVE_SHOUT2="yes"
    AC_SUBST(SHOUT2_CFLAGS)
    AC_SUBST(SHOUT2_LIBS)
  ], [
    AM_PATH_SHOUT2(HAVE_SHOUT2="yes", HAVE_SHOUT2="no")
    AC_SUBST(SHOUT2_CFLAGS)
    AC_SUBST(SHOUT2_LIBS)
  ])
])

dnl *** sidplay : works with libsidplay 1.36.x (not 2.x.x) ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SIDPLAY, true)
GST_CHECK_FEATURE(SIDPLAY, [sidplay plug-in], sidplay, [
  GST_PATH_SIDPLAY()
])

dnl *** mad ***
dnl FIXME: we could use header checks here as well IMO
translit(dnm, m, l) AM_CONDITIONAL(USE_MAD, true)
GST_CHECK_FEATURE(MAD, [mad mp3 decoder], mad, [
  dnl check with pkg-config first
  PKG_CHECK_MODULES(MAD, mad >= 0.15 id3tag >= 0.15, HAVE_MAD="yes", HAVE_MAD="no")
  if test "x$HAVE_MAD" = "xno"; then
    dnl fall back to oldskool detection
    AC_CHECK_LIB(mad, mad_decoder_finish, HAVE_MAD="yes" MAD_LIBS="-lmad")
    if test "x$HAVE_MAD" = "xyes"; then
      HAVE_MAD="no"
      save_LIBS=$LIBS
      LIBS="-lz"
      AC_CHECK_LIB(id3tag, id3_tag_options, HAVE_MAD="yes" MAD_LIBS="-lmad -lid3tag -lz")
      LIBS=$save_LIBS
    fi
  fi    
])
AC_SUBST(MAD_LIBS)

dnl **** ESound ****
translit(dnm, m, l) AM_CONDITIONAL(USE_ESD, true)
GST_CHECK_FEATURE(ESD, [esound plug-ins], esdsink, [
  PKG_CHECK_MODULES(ESD, esound >= 0.2.12, [
    HAVE_ESD="yes"
    AC_SUBST(ESD_CFLAGS)
    AC_SUBST(ESD_LIBS)
  ], [
    AM_PATH_ESD(0.2.12, HAVE_ESD="yes", HAVE_ESD="no")
    AS_SCRUB_INCLUDE(ESD_CFLAGS)
  ])
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

AC_CONFIG_FILES(
Makefile
gst-plugins.spec
gst/Makefile
gst/alpha/Makefile
gst/avi/Makefile
gst/effectv/Makefile
gst/goom/Makefile
gst/law/Makefile
gst/level/Makefile
gst/realmedia/Makefile
gst/rtp/Makefile
gst/rtsp/Makefile
gst/smpte/Makefile
gst/udp/Makefile
gst/videobox/Makefile
gst/videofilter/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
sys/Makefile
ext/Makefile
ext/aalib/Makefile
ext/libcaca/Makefile
ext/mad/Makefile
ext/shout2/Makefile
ext/sidplay/Makefile
ext/esd/Makefile
common/Makefile
common/m4/Makefile
m4/Makefile
)
AC_OUTPUT

echo -n "configure: *** Plug-ins that will be built :"
echo -e "$GST_PLUGINS_YES" | sort
echo
echo -n "configure: *** Plug-ins that will not be built :"
echo -e "$GST_PLUGINS_NO" | sort
echo
if test "x$BUILD_EXTERNAL" = "xno"; then
  echo "configure: *** No external plug-ins will be built"
fi
