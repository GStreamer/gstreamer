dnl autoconf configuration file for gst-ffmpeg 
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
AS_VERSION(gst-ffmpeg, GST_FFMPEG_VERSION, 0, 9, 3, 1,
    GST_CVS="no", GST_CVS="yes")

dnl we cheat here so we don't have to change the actual configure code bit
GST_PLUGINS_VERSION_NANO=$GST_FFMPEG_VERSION_NANO

dnl required GStreamer version
GST_REQ=0.9.1

AM_INIT_AUTOMAKE($PACKAGE,$VERSION)

dnl our libraries and install dirs use major.minor as a version
#GST_MAJORMINOR=$GST_FFMPEG_VERSION_MAJOR.$GST_FFMPEG_VERSION_MINOR
dnl hack while GST_MAJORMINOR doesn't match the release
GST_MAJORMINOR=0.9
AC_SUBST(GST_MAJORMINOR)
                                                                                
dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
AS_LIBTOOL(GST_FFMPEG, 1, 0, 0)
AM_PROG_LIBTOOL()

AC_CONFIG_SRCDIR(configure.ac)
AM_CONFIG_HEADER(config.h)

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4 $ACLOCAL_FLAGS")

AC_PROG_CC
AM_PROG_CC_STDC
AS="${CC}"

dnl decide on error flags
AS_COMPILER_FLAG(-Wall, GST_WALL="yes", GST_WALL="no")

if test "x$GST_WALL" = "xyes"; then
   GST_ERROR="$GST_ERROR -Wall"

   if test "x$GST_CVS" = "xyes"; then
     AS_COMPILER_FLAG(-Werror,GST_ERROR="$GST_ERROR -Werror",GST_ERROR="$GST_ERROR")
     GST_ERROR="$GST_ERROR -DG_DISABLE_DEPRECATED"
   fi
fi

AC_HEADER_STDC([])


dnl check for gstreamer; uninstalled is selected preferentially -- see pkg-config(1)
PKG_CHECK_MODULES(GST, gstreamer-$GST_MAJORMINOR >= $GST_REQ \
		       gstreamer-plugins-base-$GST_MAJORMINOR,
  HAVE_GST="yes", HAVE_GST="no")

if test "x$HAVE_GST" = "xno"; then
  AC_MSG_ERROR(no GStreamer found)
fi

dnl ============================= gst plug-ins ================================

plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"
AC_SUBST(plugindir)

GST_PLUGIN_LDFLAGS='-module -avoid-version -export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*'
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl Determine CPU
GST_ARCH()

dnl ###########################
dnl # Configure external libs #
dnl ###########################

AC_DEFINE_UNQUOTED(HAVE_FFMPEG_UNINSTALLED, 1,
                   [Defined if building against uninstalled FFmpeg source])
ac_configure_args="$ac_configure_args --disable-v4l --disable-audio-oss --disable-dv1394 --disable-vhook --disable-ffmpeg --disable-ffserver --enable-pp"
AC_CONFIG_SUBDIRS(gst-libs/ext/ffmpeg)

AC_CONFIG_FILES(
Makefile
gst-ffmpeg.spec
ext/Makefile
ext/ffmpeg/Makefile
ext/libpostproc/Makefile
gst-libs/Makefile
gst-libs/ext/Makefile
)
AC_OUTPUT
