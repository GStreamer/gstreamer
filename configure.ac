dnl autoconf configuration file for gst-plugins 
AC_INIT
AC_CANONICAL_TARGET([])

dnl We disable static building for development, for time savings
dnl this goes before AS_LIBTOOL to appease autoconf
dnl *NOTE*: dnl this line before release, so release does static too
AM_DISABLE_STATIC

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AS_VERSION(gst-ffmpeg, GST_FFMPEG_VERSION, 0, 7, 2, 1, GST_CVS="no", GST_CVS="yes")

AM_INIT_AUTOMAKE($PACKAGE,$VERSION)

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$GST_FFMPEG_VERSION_MAJOR.$GST_FFMPEG_VERSION_MINOR
AC_SUBST(GST_MAJORMINOR)
                                                                                
dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
AS_LIBTOOL(GST_FFMPEG, 1, 0, 0)

AC_CONFIG_SRCDIR(configure.ac)
AM_CONFIG_HEADER(config.h)

dnl Add parameters for aclocal
dnl (This must come after AM_INIT_AUTOMAKE, since it modifies ACLOCAL)
ACLOCAL_FLAGS="-I m4 -I common/m4"
AC_SUBST(ACLOCAL_AMFLAGS, $ACLOCAL_FLAGS)

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
PKG_CHECK_MODULES(GST, gstreamer-$GST_MAJORMINOR >= $GST_FFMPEG_VERSION_MAJOR.$GST_FFMPEG_VERSION_MINOR.$GST_FFMPEG_VERSION_MICRO,
  HAVE_GST="yes", HAVE_GST="no")
                                                                                
if test "x$HAVE_GST" = "xno"; then
  AC_MSG_ERROR(no GStreamer found)
fi

dnl ============================= gst plug-ins ================================

plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"
AC_SUBST(plugindir)

GST_PLUGIN_LDFLAGS='-module -avoid-version'
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl *** FFMPEG ***
translit(dnm, m, l) AM_CONDITIONAL(USE_FFMPEG, true)
GST_CHECK_FEATURE(FFMPEG, [ffmpeg plug-ins], ffmpeg, [
  # only slurp in the case where we are in CVS mode;
  # prerelease and release should get it disted
  if test "x$GST_PLUGINS_VERSION_NANO" = x1; then
    AC_MSG_NOTICE(slurping FFmpeg CVS source)
    AS_SLURP_FFMPEG(gst-libs/ext/ffmpeg, 2003-10-26 10:00 GMT,
                    HAVE_FFMPEG=yes, HAVE_FFMPEG=no)
  else
    AC_MSG_NOTICE(FFmpeg CVS code should be included already)
    HAVE_FFMPEG=yes
  fi
  # we only bother with uninstalled (included) ffmpeg for now
  AC_DEFINE_UNQUOTED(HAVE_FFMPEG_UNINSTALLED, 1,
                     [defined if we use uninstalled ffmpeg])
])

dnl Determine CPU
case "x${target_cpu}" in
  xi?86 | k?) HAVE_CPU_I386=yes
              AC_DEFINE(HAVE_CPU_I386, 1, [Define if the target CPU is an x86])
              dnl FIXME could use some better detection
              dnl       (ie CPUID)
              case "x${target_cpu}" in
                xi386 | xi486) ;;
                *)             AC_DEFINE(HAVE_RDTSC, 1, [Define if RDTSC is available]) ;;
              esac ;;
  xpowerpc)   HAVE_CPU_PPC=yes
              AC_DEFINE(HAVE_CPU_PPC, 1, [Define if the target CPU is a PPC]) ;;  xalpha)     HAVE_CPU_ALPHA=yes
              AC_DEFINE(HAVE_CPU_ALPHA, 1, [Define if the target CPU is an Alpha]) ;;
  xarm*)      HAVE_CPU_ARM=yes
              AC_DEFINE(HAVE_CPU_ARM, 1, [Define if the target CPU is an ARM]) ;;
  xsparc*)    HAVE_CPU_SPARC=yes
              AC_DEFINE(HAVE_CPU_SPARC, 1, [Define if the target CPU is a PPC]) ;;

  xmips*)     HAVE_CPU_MIPS=yes
              AC_DEFINE(HAVE_CPU_MIPS, 1, [Define if the target CPU is a MIPS]) ;;
  xhppa*)     HAVE_CPU_HPPA=yes
              AC_DEFINE(HAVE_CPU_HPPA, 1, [Define if the target CPU is a HPPA]) ;;
esac

AM_CONDITIONAL(HAVE_CPU_I386,       test "x$HAVE_CPU_I386" = "xyes")
AM_CONDITIONAL(HAVE_CPU_PPC,        test "x$HAVE_CPU_PPC" = "xyes")
AM_CONDITIONAL(HAVE_CPU_ALPHA,      test "x$HAVE_CPU_ALPHA" = "xyes")
AM_CONDITIONAL(HAVE_CPU_ARM,        test "x$HAVE_CPU_ARM" = "xyes")
AM_CONDITIONAL(HAVE_CPU_SPARC,      test "x$HAVE_CPU_SPARC" = "xyes")


dnl ###########################
dnl # Configure external libs #
dnl ###########################
if test "x$HAVE_FFMPEG" = xyes; then
  AC_CONFIG_SUBDIRS(gst-libs/ext/ffmpeg/ffmpeg)
fi 

AC_CONFIG_FILES(
Makefile
ext/Makefile
ext/ffmpeg/Makefile
gst-libs/Makefile
gst-libs/ext/Makefile
gst-libs/ext/ffmpeg/Makefile
)
AC_OUTPUT
