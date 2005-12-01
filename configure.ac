AC_PREREQ(2.52)

dnl initialize autoconf
dnl we don't specify PACKAGE and VERSION because AS_VERSION puts it together
AC_INIT

dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AS_VERSION(gst-plugins-good, GST_PLUGINS_GOOD_VERSION, 0, 9, 7, 0,
    GST_CVS="no", GST_CVS="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/law/alaw.c])

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
GST_MAJORMINOR=$GST_PLUGINS_GOOD_VERSION_MAJOR.$GST_PLUGINS_GOOD_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)

AS_LIBTOOL_TAGS([CXX])
AM_PROG_LIBTOOL

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl set up gettext
dnl the version check needs to stay here because autopoint greps for it
AM_GNU_GETTEXT_VERSION([0.11.5])
AM_GNU_GETTEXT([external])
GST_GETTEXT([gst-plugins-good-$GST_MAJORMINOR])

dnl *** check for arguments to configure ***

GST_ARG_DEBUG
GST_ARG_PROFILING
GST_ARG_VALGRIND
GST_ARG_GCOV

GST_ARG_EXAMPLES

GST_ARG_WITH_PKG_CONFIG_PATH
GST_ARG_WITH_PACKAGE_NAME([GStreamer Good Plug-ins])
GST_ARG_WITH_PACKAGE_ORIGIN

dnl these are all the gst plug-ins, compilable without additional libs
dnl videofilter is at the top because others depend on it
GST_PLUGINS_ALL="\
		videofilter \
		alpha \
		auparse \
		autodetect \
		avi \
		cutter \
		debug \
		effectv \
		flx \
		goom \
		law \
		level \
		matroska \
		multipart \
		rtp	\
		rtsp	\
		smpte	\
		udp     \
		videobox \
		videomixer \
		wavenc \
		wavparse \
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

dnl Determine endianness
AC_C_BIGENDIAN

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

AS_PROG_OBJC

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for gconftool-2
dnl this macro defines an am conditional, so it needs to be run always
AM_GCONF_SOURCE_2

dnl check for documentation tools
GTK_DOC_CHECK([1.3])
AS_PATH_PYTHON([2.1])

dnl *** checks for header files ***

dnl check if we have ANSI C header files
AC_HEADER_STDC

dnl used in gst/rtp/gstasteriskh263.c
AC_CHECK_HEADERS([netinet/in.h])
AC_CHECK_HEADERS([winsock2.h])
AM_CONDITIONAL(HAVE_WINSOCK2_H, test "x$HAVE_WINSOCK2_H" = "xyes")

dnl *** checks for compiler characteristics ***

dnl FIXME: check if this is used; was used for floatcast.h in base
dnl Check for fast float to int casting as defined in C99
AC_C99_FUNC_LRINT
AC_C99_FUNC_LRINTF

dnl *** checks for library functions ***

dnl Check for mmap (needed by electricfence plugin)
AC_FUNC_MMAP
AM_CONDITIONAL(GST_HAVE_MMAP, test "x$ac_cv_func_mmap_fixed_mapped" == "xyes")

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
GST_REQ=0.9.7
GSTPB_REQ=0.9.7
GST_CHECK_GST($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_BASE($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_GDP($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_CONTROLLER($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_CHECK($GST_MAJORMINOR, [$GST_REQ], no)
GST_CHECK_GST_PLUGINS_BASE($GST_MAJORMINOR, [$GSTPB_REQ])

GST_TOOLS_DIR=`pkg-config --variable=toolsdir gstreamer-$GST_MAJORMINOR`
if test -z $GST_TOOLS_DIR; then
  AC_MSG_ERROR([no tools dir defined in GStreamer pkg-config file; core upgrade needed.])
fi
AC_SUBST(GST_TOOLS_DIR)

dnl FIXME: get rid of this by making sure gstreamer-check brings it in
dnl check for "check", unit testing library/header
AM_PATH_CHECK(0.9.2, HAVE_CHECK=yes, HAVE_CHECK=no)
AM_CONDITIONAL(HAVE_CHECK, test "x$HAVE_CHECK" = "xyes")

dnl should we install schemas ?
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
       AC_DEFINE(HAVE_OSS_INCLUDE_IN_MACHINE,,
         [Define if OSS includes are in /machine/])
       ], [
       HAVE_OSS="no"
     ])
   ])
 ])
])

dnl *** ext plug-ins ***
dnl keep this list sorted alphabetically !

if test "x$BUILD_EXTERNAL" = "xyes"; then

echo
AC_MSG_NOTICE([Checking libraries for plugins in ext/])
echo

dnl *** aalib ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AALIB, true)
GST_CHECK_FEATURE(AALIB, [aasink plug-in], aasink, [
  AM_PATH_AALIB(, HAVE_AALIB=yes, HAVE_AALIB=no)
  AS_SCRUB_INCLUDE(AALIB_CFLAGS)
])

dnl *** cairo ***
translit(dnm, m, l) AM_CONDITIONAL(USE_CAIRO, true)
GST_CHECK_FEATURE(CAIRO, [cairo plug-in], cairo, [
  PKG_CHECK_MODULES(CAIRO, cairo >= 1.0.0, [
    HAVE_CAIRO=yes
    AC_SUBST(CAIRO_CFLAGS)
    AC_SUBST(CAIRO_LIBS)
  ], [
     HAVE_CAIRO=no
  ])
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

dnl *** GConf ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GCONF, true)
GST_CHECK_FEATURE(GCONF, [GConf libraries], , [
  PKG_CHECK_MODULES(GCONF, gconf-2.0, HAVE_GCONF="yes", HAVE_GCONF="no")
  AC_SUBST(GCONF_CFLAGS)
  AC_SUBST(GCONF_LIBS)
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

fi dnl of EXT plugins

dnl *** finalize CFLAGS, LDFLAGS, LIBS

dnl Overview:
dnl GST_OPTION_CFLAGS:  common flags for profiling, debugging, errors, ...
dnl GST_*:              flags shared by all built objects
dnl GST_ALL_LDFLAGS:    linker flags shared by all
dnl GST_LIB_LDFLAGS:    not needed, we don't install libraries
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
dnl add ERROR_CFLAGS, but overridable
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

dnl keep this alphabetic per directory, please
AC_CONFIG_FILES(
Makefile
gst/Makefile
gst/alpha/Makefile
gst/auparse/Makefile
gst/autodetect/Makefile
gst/avi/Makefile
gst/cutter/Makefile
gst/debug/Makefile
gst/effectv/Makefile
gst/goom/Makefile
gst/law/Makefile
gst/level/Makefile
gst/matroska/Makefile
gst/multipart/Makefile
gst/rtp/Makefile
gst/rtsp/Makefile
gst/smpte/Makefile
gst/udp/Makefile
gst/videobox/Makefile
gst/videofilter/Makefile
gst/videomixer/Makefile
gst/wavenc/Makefile
gst/wavparse/Makefile
gst/flx/Makefile
ext/jpeg/Makefile
ext/Makefile
ext/aalib/Makefile
ext/cairo/Makefile
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
tests/Makefile
tests/check/Makefile
common/Makefile
common/m4/Makefile
m4/Makefile
gst-plugins-good.spec
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
