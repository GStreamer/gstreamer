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
AS_VERSION(gst-plugins-good, GST_PLUGINS_GOOD_VERSION, 0, 9, 3, 1,
    GST_CVS="no", GST_CVS="yes")

AM_INIT_AUTOMAKE($PACKAGE,$VERSION)

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$GST_PLUGINS_GOOD_VERSION_MAJOR.$GST_PLUGINS_GOOD_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_MAJORMINOR=0.9
AC_SUBST(GST_MAJORMINOR)

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
dnl for 0.8.3 release, gst_play_get_all_by_interface was added, so update
AS_LIBTOOL(GST_PLUGINS_GOOD, 0, 0, 0)
AS_LIBTOOL_TAGS([CXX])
AM_PROG_LIBTOOL

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
AM_GNU_GETTEXT_VERSION(0.11.5)
AM_GNU_GETTEXT([external])
                                                                            
GETTEXT_PACKAGE=gst-plugins-good-$GST_MAJORMINOR
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

PKG_CHECK_MODULES(GST_CHECK, gstreamer-check-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_CHECK="yes", HAVE_GST_CHECK="no")

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

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

GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_desc\$\$' -no-undefined"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl these are all the gst plug-ins, compilable without additional libs
dnl videofilter is at the top because others depend on it
GST_PLUGINS_ALL="\
		videofilter \
		alpha \
		auparse \
		autodetect \
		avi \
		debug \
		effectv \
		goom \
		law \
		level \
		rtp	\
		rtsp	\
		smpte	\
		udp     \
		videobox \
		wavenc \
		wavparse \
                "

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

dnl Check for mmap (needed by electricfence plugin)
AC_FUNC_MMAP()
AM_CONDITIONAL(GST_HAVE_MMAP, test "x$ac_cv_func_mmap_fixed_mapped" == "xyes")

dnl ==========================================================================
dnl ============================= sys plug-ins ================================
dnl ==========================================================================

dnl *** OSS audio *** (Linux, *BSD)
translit(dnm, m, l) AM_CONDITIONAL(USE_OSS, true)
GST_CHECK_FEATURE(OSS, [OSS audio], osssrc osssink, [
 HAVE_OSS="yes"
dnl Linux and newer BSD versions :
 AC_CHECK_HEADER(sys/soundcard.h, [
   AC_DEFINE(HAVE_OSS_INCLUDE_IN_SYS,, [Define if OSS includes are in /sys/])
   ] , [
dnl Some old BSD versions :
   AC_CHECK_HEADER(soundcard.h, [
     AC_DEFINE(HAVE_OSS_INCLUDE_IN_ROOT,, [Define if OSS includes are in /])
     ], [
  dnl Some old BSD versions :
     AC_CHECK_HEADER(machine/soundcard.h, [
       AC_DEFINE(HAVE_OSS_INCLUDE_IN_MACHINE,, [Define if OSS includes are in /machine/])
       ], [
       HAVE_OSS="no"
     ])
   ])
 ])
])

dnl ###########################
dnl # Configure external libs #
dnl ###########################

dnl *** aalib ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AALIB, true)
GST_CHECK_FEATURE(AALIB, [aasink plug-in], aasink, [
  AM_PATH_AALIB(, HAVE_AALIB=yes, HAVE_AALIB=no)
  AS_SCRUB_INCLUDE(AALIB_CFLAGS)
])

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

dnl *** FLAC ***
translit(dnm, m, l) AM_CONDITIONAL(USE_FLAC, true)
GST_CHECK_FEATURE(FLAC, [FLAC lossless audio], flacenc flacdec, [
  GST_CHECK_LIBHEADER(FLAC, FLAC, FLAC__seekable_stream_encoder_new, -lm, FLAC/all.h, FLAC_LIBS="-lFLAC -lm")
  dnl API change in FLAC 1.1.1, so require that...
  if test x$HAVE_FLAC = xyes; then
    AC_CHECK_DECL(FLAC__SEEKABLE_STREAM_ENCODER_TELL_ERROR,
                  HAVE_FLAC="yes", HAVE_FLAC="no", [
#include <FLAC/seekable_stream_encoder.h>
                  ])
  fi
  AC_SUBST(FLAC_LIBS)
])

dnl *** jpeg ***
dnl FIXME: we could use header checks here as well IMO
translit(dnm, m, l) AM_CONDITIONAL(USE_JPEG, true)
GST_CHECK_FEATURE(JPEG, [jpeg], jpegenc jpegdec, [
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


dnl *** ladspa ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LADSPA, true)
GST_CHECK_FEATURE(LADSPA, [ladspa], ladspa, [
  AC_CHECK_HEADER(ladspa.h, HAVE_LADSPA="yes", HAVE_LADSPA="no")
])

dnl *** libcaca ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBCACA, true)
GST_CHECK_FEATURE(LIBCACA, [libcaca], libcaca, [
  GST_CHECK_CONFIGPROG(LIBCACA, caca-config)
  AC_SUBST(LIBCACA_CFLAGS)
  AC_SUBST(LIBCACA_LIBS)
])

dnl *** libdv ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBDV, true)
GST_CHECK_FEATURE(LIBDV, [libdv DV/video decoder], dvdec, [
  PKG_CHECK_MODULES(LIBDV, libdv >= 0.100, HAVE_LIBDV="yes", HAVE_LIBDV="no")
  AC_SUBST(LIBDV_CFLAGS)
  AC_SUBST(LIBDV_LIBS)
])

dnl *** libpng ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBPNG, true)
GST_CHECK_FEATURE(LIBPNG, [libpng PNG encoder], pngenc, [
  PKG_CHECK_MODULES(LIBPNG, libpng12, HAVE_LIBPNG="yes", HAVE_LIBPNG="no")
  AC_SUBST(LIBPNG_CFLAGS)
  AC_SUBST(LIBPNG_LIBS)
])

dnl *** dv1394 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DV1394, true)
GST_CHECK_FEATURE(DV1394, [raw1394 and avc1394 library], dv1394src, [
  dnl we need to test three headers and three libs
  GST_CHECK_LIBHEADER(RAW1394,
    raw1394, raw1394_new_handle,,
    libraw1394/raw1394.h, RAW1394_LIBS="-lraw1394")
  GST_CHECK_LIBHEADER(AVC1394,
    avc1394, avc1394_send_command, $RAW1394_LIBS,
    libavc1394/avc1394.h, AVC1394_LIBS="-lavc1394")
  GST_CHECK_LIBHEADER(ROM1394,
    rom1394, rom1394_free_directory, $RAW1394_LIBS,
    libavc1394/rom1394.h, ROM1394_LIBS="-lrom1394")

  dnl now see how far we got
  if test x$HAVE_RAW1394 = xyes && \
     test x$HAVE_AVC1394 = xyes && \
     test x$HAVE_ROM1394 = xyes; then
       HAVE_DV1394=yes
       DV1394_LIBS="$RAW1394_LIBS $AVC1394_LIBS $ROM1394_LIBS"
       AC_SUBST(DV1394_LIBS)
  else
       HAVE_DV1394=no
  fi
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

dnl *** speex >= 1.0.4 or >= 1.1.5 ***
dnl   1.1.4 and earlier were not API/ABI compatible with 1.0
dnl   1.1.6 is the first to use a .pc/pkg-config file ***
dnl   speex_jitter.h is 1.1.x only
translit(dnm, m, l) AM_CONDITIONAL(USE_SPEEX, true)
GST_CHECK_FEATURE(SPEEX, [speex plug-in], speex, [
  PKG_CHECK_MODULES(SPEEX, speex >= 1.1.6, [
    HAVE_SPEEX="yes"
    AC_SUBST(SPEEX_CFLAGS)
    AC_SUBST(SPEEX_LIBS)
  ], [
    GST_CHECK_LIBHEADER(SPEEX, speex, speex_bits_init, , speex/speex.h, [
      AC_CHECK_HEADER(speex/speex_jitter.h, [
        dnl speex 1.1.x :
        GST_CHECK_LIBHEADER(SPEEX, speex, speex_encode_int, , speex/speex.h, [
          dnl speex 1.1.5 or + :
          HAVE_SPEEX="yes"
          SPEEX_LIBS="-lspeex"
          AC_SUBST(SPEEX_CFLAGS)
          AC_SUBST(SPEEX_LIBS)
        ],[
          HAVE_SPEEX="no"
        ])
      ],[
        dnl speex 1.0.x :
          AC_CHECK_DECL(SPEEX_GET_LOOKAHEAD, [
            dnl speex 1.0.4
            HAVE_SPEEX="yes"
            SPEEX_LIBS="-lspeex"
            AC_SUBST(SPEEX_CFLAGS)
            AC_SUBST(SPEEX_LIBS)

            AC_DEFINE_UNQUOTED(SPEEX_1_0, 1,
              [defined if speex 1.0.x API detected])
        ],[
            HAVE_SPEEX="no"
            AC_MSG_NOTICE(You need at least 1.0.4 to compile the speex plugin)
        ], [
#include <speex/speex.h>
        ])
      ])
    ])
  ])
])

dnl also add builddir include for enumtypes and marshal
GST_CFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs $GST_CFLAGS $GST_ERROR"

AC_SUBST(GST_LIBS)
AC_SUBST(GST_CFLAGS)

dnl ######################
dnl # Checks for gtk-doc #
dnl ######################

GTK_DOC_CHECK([1.3])
AS_PATH_PYTHON([2.1])

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

dnl keep this alphabetic per directory, please
AC_CONFIG_FILES(
Makefile
gst/Makefile
gst/alpha/Makefile
gst/auparse/Makefile
gst/autodetect/Makefile
gst/avi/Makefile
gst/debug/Makefile
gst/effectv/Makefile
gst/goom/Makefile
gst/law/Makefile
gst/level/Makefile
gst/rtp/Makefile
gst/rtsp/Makefile
gst/smpte/Makefile
gst/udp/Makefile
gst/videobox/Makefile
gst/videofilter/Makefile
gst/wavenc/Makefile
gst/wavparse/Makefile
ext/jpeg/Makefile
ext/Makefile
ext/aalib/Makefile
ext/dv/Makefile
ext/esd/Makefile
ext/flac/Makefile
ext/gconf/Makefile
ext/ladspa/Makefile
ext/libcaca/Makefile
ext/libpng/Makefile
ext/raw1394/Makefile
ext/shout2/Makefile
ext/speex/Makefile
sys/Makefile
sys/oss/Makefile
po/Makefile.in
gconf/Makefile
gconf/gstreamer.schemas
docs/Makefile
docs/plugins/Makefile
docs/version.entities
check/Makefile
common/Makefile
common/m4/Makefile
m4/Makefile
gst-plugins-good.spec
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
