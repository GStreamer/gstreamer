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
AS_VERSION(gst-plugins, GST_PLUGINS_VERSION, 0, 7, 2, 1, GST_CVS="no", GST_CVS="yes")

dnl add a suffix to apps
if test x$program_suffix = xNONE ; then
  program_suffix=-$GST_PLUGINS_VERSION_MAJOR.$GST_PLUGINS_VERSION_MINOR
fi

AM_INIT_AUTOMAKE($PACKAGE,$VERSION)

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$GST_PLUGINS_VERSION_MAJOR.$GST_PLUGINS_VERSION_MINOR
AC_SUBST(GST_MAJORMINOR)

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
AS_LIBTOOL(GST_PLUGINS, 1, 0, 0)

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

dnl decide on error flags
AS_COMPILER_FLAG(-Wall, GST_WALL="yes", GST_WALL="no")

if test "x$GST_WALL" = "xyes"; then
   GST_ERROR="$GST_ERROR -Wall"

   if test "x$GST_CVS" = "xyes"; then
     AS_COMPILER_FLAG(-Werror,GST_ERROR="$GST_ERROR -Werror",GST_ERROR="$GST_ERROR")
     GST_ERROR="$GST_ERROR -DG_DISABLE_DEPRECATED"
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

dnl Check for a way to display the function name in debug output
GST_CHECK_FUNCTION()

dnl define correct errorlevel for debugging messages. We want to have GST_ERROR
dnl messages printed when running cvs builds
if test "x$GST_CVS" = "xyes"; then
  AC_DEFINE(GST_LEVEL_DEFAULT, GST_LEVEL_ERROR, [Default errorlevel to use])
fi

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
  USE_SHOUT2="yes"
],[
  AC_MSG_NOTICE(not building experimental plug-ins)
  USE_TARKIN="no"
  USE_SHOUT2="no"
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

dnl Check for nasm
AC_PATH_PROG(NASM_PATH, nasm, no)
AC_SUBST(NASM_PATH)
if test x$NASM_PATH = xno; then
  AC_MSG_WARN(Couldn't find nasm)
  HAVE_NASM="no"
else
  AC_DEFINE(HAVE_NASM, 1, [Define if NASM, the netwide assembler, is available])
  HAVE_NASM="yes"
fi

dnl check for gconftool-2
translit(dnm, m, l) AM_CONDITIONAL(USE_GCONFTOOL, true)
GST_CHECK_FEATURE(GCONFTOOL, [GConf schemas], , [
  AC_PATH_PROG(GCONFTOOL, gconftool-2, no)
  if test x$GCONFTOOL = xno; then
    AC_MSG_WARN(Not installing GConf schemas)
    HAVE_GCONFTOOL="no"
  else
    AM_GCONF_SOURCE_2
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

dnl check for gstreamer; uninstalled is selected preferentially -- see pkg-config(1)
PKG_CHECK_MODULES(GST, gstreamer-$GST_MAJORMINOR >= $GST_PLUGINS_VERSION_MAJOR.$GST_PLUGINS_VERSION_MINOR.$GST_PLUGINS_VERSION_MICRO,
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
PKG_CHECK_MODULES(GST_CONTROL, gstreamer-control-$GST_MAJORMINOR >= $GST_PLUGINS_VERSION_MAJOR.$GST_PLUGINS_VERSION_MINOR.$GST_PLUGINS_VERSION_MICRO,
  HAVE_GST_CONTROL="yes", HAVE_GST_CONTROL="no")

if test "x$HAVE_GST_CONTROL" = "xno"; then
  AC_MSG_ERROR(no GStreamer Control Libs found)
fi

AC_SUBST(GST_CONTROL_LIBS)

dnl Set up conditionals for (target) architecture:
dnl ==============================================

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
              AC_DEFINE(HAVE_CPU_PPC, 1, [Define if the target CPU is a PPC]) ;;
  xalpha)     HAVE_CPU_ALPHA=yes
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
# make these available to automake
AM_CONDITIONAL(HAVE_CPU_I386,       test "x$HAVE_CPU_I386" = "xyes")
AM_CONDITIONAL(HAVE_CPU_PPC,        test "x$HAVE_CPU_PPC" = "xyes")
AM_CONDITIONAL(HAVE_CPU_ALPHA,      test "x$HAVE_CPU_ALPHA" = "xyes")
AM_CONDITIONAL(HAVE_CPU_ARM,        test "x$HAVE_CPU_ARM" = "xyes")
AM_CONDITIONAL(HAVE_CPU_SPARC,      test "x$HAVE_CPU_SPARC" = "xyes")

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
dnl GTK
HAVE_GTK=NO
PKG_CHECK_MODULES(GTK2, gtk+-2.0 >= 2.2.0, HAVE_GTK_22=yes, HAVE_GTK_22=no)
if test "x$HAVE_GTK_22" = "xyes"; then
  HAVE_GTK=yes
  AC_PATH_PROG(QUERYLOADERS, gdk-pixbuf-query-loaders, no)
  GTK_VERSION=`$PKG_CONFIG --variable=gtk_binary_version gtk+-2.0`
  AC_SUBST(GTK_VERSION)
  GTK_PREFIX=`$PKG_CONFIG --variable=prefix gdk-pixbuf-2.0`
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
AM_CONDITIONAL(HAVE_GDK_LOADERS, test "x$HAVE_GTK_22" = "xyes")

# allow customization of pixbuf loader install location
# when nothing specified, adhere to prefix settings
# when called without any option with this argument, autodetect
# when called with a path, set to the given path
AC_ARG_WITH(gdk-pixbuf-loader-dir, 
   AC_HELP_STRING([--with-gdk-pixbuf-loader-dir],
   	[directory to install the gdk_pixbuf loader (none for pkg-config default)]),
   [
    if test "x${withval}" != xyes ; then
   	GDK_PIXBUF_LOADER_DIR="${withval}"
    else
      GDK_PIXBUF_LOADER_DIR="$GDK_PIXBUF_LIBDIR/gtk-2.0/\$GTK_VERSION/loaders"
    fi
   ],
   # nothing specified
   GDK_PIXBUF_LOADER_DIR=${libdir}/gtk-2.0/\$GTK_VERSION/loaders
)
AS_AC_EXPAND(GDK_PIXBUF_LOADER_DIR, $GDK_PIXBUF_LOADER_DIR)
AC_SUBST(GDK_PIXBUF_LOADER_DIR)
AC_MSG_NOTICE([Putting GTK+-2 pixbuf loaders in $GDK_PIXBUF_LOADER_DIR])

# allow customization of pixbuf loader configuration file
# when nothing specified, adhere to prefix settings
# when called without any option with this argument, autodetect
# when called with a path, set to the given path
AC_ARG_WITH(gdk-pixbuf-conf-dir, 
   AC_HELP_STRING([--with-gdk-pixbuf-conf-dir],
   	[directory to install the gdk_pixbuf config (none for pkg-config default)]),
   [
    if test "x${withval}" != xyes ; then
      GDK_PIXBUF_CONF_DIR="${withval}"
    else
      GDK_PIXBUF_CONF_DIR="$GDK_PIXBUF_PREFIXDIR/etc/gtk-2.0/"
    fi
   ],
   # nothing specified
   GDK_PIXBUF_LOADER_DIR=${libdir}/gtk-2.0/\$GTK_VERSION/loaders
   GDK_PIXBUF_CONF_DIR=${sysconfdir}/gtk-2.0
)
AS_AC_EXPAND(GDK_PIXBUF_CONF_DIR, $GDK_PIXBUF_CONF_DIR)
AC_SUBST(GDK_PIXBUF_CONF_DIR)
AC_MSG_NOTICE([Putting GTK+-2 pixbuf loader config in $GDK_PIXBUF_CONF_DIR])

dnl ===========================================================================
dnl ============================= gst plug-ins ================================
dnl ===========================================================================

plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"
AC_SUBST(plugindir)

GST_PLUGIN_LDFLAGS='-module -avoid-version'
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl these are all the gst plug-ins, compilable without additional libs
GST_PLUGINS_ALL="\
	ac3parse \
	adder \
	asfdemux \
	audioconvert \
	audioscale \
	auparse \
	avi \
	cdxaparse \
	chart \
	cutter \
	debug \
	deinterlace \
	effectv \
	festival \
	filter \
	flx \
	goom \
	id3 \
	intfloat \
	law \
	level \
	matroska \
	median \
	mixmatrix \
	mpeg1sys \
	mpeg1videoparse \
	mpeg2sub \
	mpegaudio \
	mpegaudioparse \
	mpegstream \
	monoscope \
	oneton \
	overlay \
	passthrough \
	playondemand \
	qtdemux \
	realmedia \
	rtjpeg \
	rtp \
	silence \
	sine \
	smooth \
	smpte \
	spectrum \
	speed \
	stereo \
	switch \
	synaesthesia \
	tags \
	tcp \
	typefind \
	udp \
	vbidec \
	videocrop \
	videodrop \
	videoflip \
	videofilter \
	videoscale \
	videotestsrc \
	volenv \
	volume \
	wavenc \
	wavparse \
	y4m"

dnl see if we can build C++ plug-ins
if test "x$HAVE_CXX" = "xyes"; then
  GST_PLUGINS_ALL="$GST_PLUGINS_ALL \
                  modplug"
else
  AC_MSG_WARN([Not compiling plug-ins requiring C++ compiler])
fi

AC_SUBST(GST_PLUGINS_ALL)

GST_PLUGINS_SELECTED=""

AC_ARG_WITH(plugins,
    AC_HELP_STRING([--with-plugins],[comma-separated list of plug-ins to compile]),
    [for i in `echo $withval | tr , ' '`; do
        if test -n `echo $i | grep $GST_PLUGINS_ALL`; then
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

dnl *** DXR3 card ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DXR3, true)
GST_CHECK_FEATURE(DXR3, [DXR3 hardware MPEG DVD decoder],
  dxr3videosink dxr3audiosink dxr3spusink, [
  HAVE_DXR3=yes
  AC_CHECK_HEADER(linux/em8300.h, ,
                  [ AC_MSG_WARN([DXR3/em8300 header file not found]) &&
                    HAVE_DXR3=no ] )
  AC_CHECK_HEADER(linux/soundcard.h, ,
                  [ AC_MSG_WARN([Generic sound header file not found]) &&
                    HAVE_DXR3=no ] )
])

dnl *** OSS audio ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OSS, true)
GST_CHECK_FEATURE(OSS, [OSS audio], osssrc osssink, [
  AC_CHECK_HEADER(sys/soundcard.h, HAVE_OSS="yes", HAVE_OSS="no")
])

dnl *** QuickCam ***
translit(dnm, m, l) AM_CONDITIONAL(USE_QCAM, true)
GST_CHECK_FEATURE(QCAM, [QuickCam], qcamsrc, [
  if test "x$HAVE_CPU_I386" != "xyes";
  then
    HAVE_QCAM="no"
  else
    AC_CHECK_HEADER(sys/io.h, HAVE_QCAM="yes", HAVE_QCAM="no")
  fi
  if test "x$HAVE_QCAM" != "xyes";
  then
    AC_MSG_WARN([QuickCam only works on i386-linux])
  fi
])

dnl *** Video CD ***
translit(dnm, m, l) AM_CONDITIONAL(USE_VCD, true)
GST_CHECK_FEATURE(VCD, [Video CD], vcdsrc, [
  AC_CHECK_HEADER(linux/cdrom.h, HAVE_VCD="yes", HAVE_VCD="no")
])

dnl *** CDROM Audio ***
translit(dnm, m, l) AM_CONDITIONAL(USE_CDROM, true)
GST_CHECK_FEATURE(CDROM, [CDROM Audio], cdrom, [
  AC_CHECK_HEADERS(linux/cdrom.h) dnl linux
  AC_CHECK_HEADERS(sys/cdio.h) dnl almost everything else
dnl  AC_CHECK_HEADERS(dmedia/cdaudio.h) dnl irix

  if test "${ac_cv_header_linux_cdrom_h}" = "yes" || test "${ac_cv_header_sys_cdio_h}" = "yes" || test "${ac_cv_header_dmedia_cdaudio_h}" = "yes"; then
	case "$host" in 
		*-sun-* | *-*-linux*)
			AC_DEFINE(HAVE_CDROM_SOLARIS,, [Define if cdrom access is in Solaris style])
		;;
		*-*-freebsd*)
			AC_DEFINE(HAVE_CDROM_BSD,, [Define if cdrom access is in BSD style])
		;;
		*-*-netbsd* | *-*-openbsd*)
			AC_DEFINE(HAVE_CDROM_BSD,, [Define if cdrom access is in BSD style])
			AC_DEFINE(HAVE_CDROM_BSD_NETBSD,, [Define if cdrom access uses NetBSD variant])
		;;
		*-*darwin*)
			AC_DEFINE(HAVE_CDROM_BSD,, [Define if cdrom access is in BSD style])
			AC_DEFINE(HAVE_CDROM_BSD_DARWIN,, [Define if cdrom access uses Darwin variant])
		;;
dnl		*-irix-*)
dnl			AC_DEFINE(HAVE_CDROM_IRIX,, [Define if cdrom access is in Irix DMedia style])
dnl		;;
    esac

	HAVE_CDROM="yes"
  else
	HAVE_CDROM="no"
  fi
])

dnl Check for X11
translit(dnm, m, l) AM_CONDITIONAL(USE_X, true)
GST_CHECK_FEATURE(X, [X libraries and plugins],
                  [ximagesink], [
  AC_PATH_XTRA
  if test "x$X_CFLAGS" == "x" -o "$X_CFLAGS" == "-DX_DISPLAY_MISSING"
  then
    AC_MSG_NOTICE([cannot find X11 development files])
    HAVE_X="no"
  else
    dnl this is much more than we want
    X_LIBS="$X_LIBS $X_PRE_LIBS $X_EXTRA_LIBS"
    dnl AC_PATH_XTRA only defines the path needed to find the X libs, not the libs
    dnl therefore we add them here
    X_LIBS="$X_LIBS -lX11"
    AC_SUBST(X_CFLAGS)
    AC_SUBST(X_LIBS)
    HAVE_X="yes"
  fi
  AC_SUBST(HAVE_X)
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
                  [xvimagesink xvideosink], [
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
translit(dnm, m, l) AM_CONDITIONAL(USE_V4L, true)
GST_CHECK_FEATURE(V4L, [Video 4 Linux], v4lsrc v4lmjpegsrc v4lmjpegsink, [
  # first check X
  HAVE_V4L="no"
  if test "$HAVE_X" == "yes"
  then
    AC_CHECK_DECL(VID_TYPE_MPEG_ENCODER, HAVE_V4L="yes", HAVE_V4L="no", [
#include <sys/types.h>
#define _LINUX_TIME_H
#include <linux/videodev.h>
    ])
  fi
])

dnl *** Video 4 Linux 2***
dnl for information about the header/define, see sys/v4l2/gstv4l2element.h
translit(dnm, m, l) AM_CONDITIONAL(USE_V4L2, true)
GST_CHECK_FEATURE(V4L2, [Video 4 Linux 2], v4l2src, [
  HAVE_V4L2="no"
  if test "$HAVE_X" == "yes"
  then
    AC_MSG_CHECKING([Checking for uptodate v4l2 installation])
    AC_TRY_COMPILE([
#include <sys/types.h>
#include <linux/types.h>
#define _LINUX_TIME_H
#include <linux/videodev2.h>
#if defined(V4L2_MAJOR_VERSION) || defined(V4L2_MINOR_VERSION)
#error too early v4l2 version or no v4l2 at all
#endif
    ], [
return 0;
    ], [ HAVE_V4L2="yes" && AC_MSG_RESULT(yes)],
       [ HAVE_V4L2="no"  && AC_MSG_RESULT(no) &&
         AC_CHECK_HEADER(linux/videodev2.h,
                         [ AC_MSG_WARN([video4linux2 headers were found, but they're old. Please update v4l2 to compile the v4l2 plugins])],
                         [ AC_MSG_WARN([video4linux2 was not found])])])
  fi
])

dnl Next, check for the optional libraries:
dnl These are all libraries used in building plug-ins
dnl ================================================
dnl let's try and sort them alphabetically, shall we ?

if test "x$BUILD_EXTERNAL" = "xyes"; then

AC_MSG_NOTICE(Checking for plug-in dependency libraries)

dnl *** a52dec ***
translit(dnm, m, l) AM_CONDITIONAL(USE_A52DEC, true)
GST_CHECK_FEATURE(A52DEC, [a52dec], a52dec, [
  AC_CHECK_A52DEC(HAVE_A52DEC=yes, HAVE_A52DEC=no)
])

dnl *** aalib ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AALIB, true)
GST_CHECK_FEATURE(AALIB, [aasink plug-in], aasink, [
  AM_PATH_AALIB(, HAVE_AALIB=yes, HAVE_AALIB=no)
  AS_SCRUB_INCLUDE(AALIB_CFLAGS)
])

dnl *** alsa ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ALSA, true)
GST_CHECK_FEATURE(ALSA, [alsa plug-ins], gstalsa, [
   AM_PATH_ALSA(0.9.1, HAVE_ALSA=yes, HAVE_ALSA=no)
])

dnl *** arts ***
dnl if mcopidl can't be found there's no use in compiling it
AC_CHECK_PROG(MCOPIDL, mcopidl, yes, no)
if test "x$HAVE_MCOPIDL" = "xno";
then
  USE_ARTS=no
fi

translit(dnm, m, l) AM_CONDITIONAL(USE_ARTS, true)
GST_CHECK_FEATURE(ARTS, [arts plug-ins], arts, [
  AM_PATH_ARTS(, HAVE_ARTS=yes, HAVE_ARTS=no)
])

dnl *** artsc ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ARTSC, true)
GST_CHECK_FEATURE(ARTSC, [artsd plug-ins], artsdsink, [
  GST_CHECK_ARTSC()
])

dnl *** audiofile ***
dnl this check uses the GST_CHECK_CONFIGPROG macro
translit(dnm, m, l) AM_CONDITIONAL(USE_AUDIOFILE, true)
GST_CHECK_FEATURE(AUDIOFILE, [audiofile], afsink afsrc, [
  translit(dnm, m, l) AC_SUBST(AUDIOFILE_LIBS)
  translit(dnm, m, l) AC_SUBST(AUDIOFILE_CFLAGS)
  dnl check with pkg-config first
  PKG_CHECK_MODULES(AUDIOFILE, audiofile, HAVE_AUDIOFILE="yes", HAVE_AUDIOFILE="no")
  if test "x$HAVE_AUDIOFILE" = "xno"; then
    GST_CHECK_CONFIGPROG(AUDIOFILE, audiofile-config)
    dnl we need this function
    AC_CHECK_LIB(audiofile, afOpenVirtualFile, , HAVE_AUDIOFILE="no")
  fi])

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

dnl *** dvdread ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DVDREAD, true)
GST_CHECK_FEATURE(DVDREAD, [dvdread library], dvdreadsrc, [
  GST_CHECK_LIBHEADER(DVDREAD, dvdread, DVDOpen, , dvdread/dvd_reader.h, DVDREAD_LIBS="-ldvdread")
  AC_SUBST(DVDREAD_LIBS)
])

dnl *** dvdnav ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DVDNAV, true)
GST_CHECK_FEATURE(DVDNAV, [dvdnav library], dvdnavsrc, [
  translit(dnm, m, l) AC_SUBST(DVDNAV_LIBS)
  translit(dnm, m, l) AC_SUBST(DVDNAV_CFLAGS)
  GST_CHECK_CONFIGPROG(DVDNAV, dvdnav-config)
  if test x"$HAVE_DVDNAV" = x"yes"; then
    dnl check version
    DVDNAV_VERSION=`dvdnav-config --version|head -n 1|sed 's/^.*) //'|sed 's/ (.*)//'`
    DVDNAV_MAJOR=`echo $DVDNAV_VERSION | cut -d. -f1 | sed s/[a-zA-Z\-].*//g`
    DVDNAV_MINOR=`echo $DVDNAV_VERSION | cut -d. -f2 | sed s/[a-zA-Z\-].*//g`
    DVDNAV_MICRO=`echo $DVDNAV_VERSION | cut -d. -f3 | sed s/[a-zA-Z\-].*//g`
    if [[ "$DVDNAV_MAJOR" -eq "0" ]] && \
       [[ "$DVDNAV_MINOR" -lt "1" ]]; then
      AC_MSG_WARN([libdvdnav >= 0.1.7 is required, you have $DVDNAV_VERSION])
      HAVE_DVDNAV="no"
    elif [[ "$DVDNAV_MAJOR" -eq "0" ]] && \
         [[ "$DVDNAV_MINOR" -eq "1" ]] && \
         [[ "$DVDNAV_MICRO" -lt "7" ]]; then
      AC_MSG_WARN([libdvdnav >= 0.1.7 is required, you have $DVDNAV_VERSION])
      HAVE_DVDNAV="no"
      fi
    fi
  AS_SCRUB_INCLUDE(DVDNAV_CFLAGS)
])

dnl *** MAS ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MAS, true)
GST_CHECK_FEATURE(MAS, [mas library], massink, [
  translit(dnm, m, l) AC_SUBST(MAS_LIBS)
  translit(dnm, m, l) AC_SUBST(MAS_CFLAGS)
  GST_CHECK_CONFIGPROG(MAS, mas-config)
  AS_SCRUB_INCLUDE(MAS_CFLAGS)
])

dnl **** ESound ****
translit(dnm, m, l) AM_CONDITIONAL(USE_ESD, true)
GST_CHECK_FEATURE(ESD, [esound plug-ins], esdsink esdmon, [
  AM_PATH_ESD(0.2.12, HAVE_ESD=yes, HAVE_ESD=no)
  AS_SCRUB_INCLUDE(ESD_CFLAGS)
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
  GST_CHECK_LIBHEADER(FAAD, faad, faacDecOpen, -lm, faad.h, FAAD_LIBS="-lfaad -lm")
  AC_MSG_CHECKING([Checking for FAAD >= 2])
  AC_TRY_COMPILE([
#include <faad.h>
#if !defined(FAAD2_VERSION) || !defined(FAAD_FMT_DOUBLE)
#error Not faad2
#endif
  ], [ return 0; ],
     [ HAVE_FAAD="yes" && AC_MSG_RESULT(yes)],
     [ HAVE_FAAD="no"  && AC_MSG_RESULT(no)])
  AS_SCRUB_INCLUDE(FAAD_CFLAGS)
  AC_SUBST(FAAD_LIBS)
])

dnl **** festival ****
dnl translit(dnm, m, l) AM_CONDITIONAL(USE_FESTIVAL, true)
dnl GST_CHECK_FEATURE(FESTIVAL, [festival plug-ins], festivalsrc, [
  dnl NOTE: just using local net connection now, add this lib check
  dnl       in the future if needed
  dnl AC_LANG_PUSH(C++)
  dnl dnl FIXME: took out func to check for
  dnl dnl This check puts festival_tidy_up in extern "C".
  dnl dnl But, at least on Debian as of 20020110, it is compiled with name
  dnl dnl mangling C++ nonsense and symbols can't resolve
  dnl dnl GST_CHECK_LIBHEADER(FESTIVAL, Festival, festival_tidy_up, , festival/festival.h, FESTIVAL_LIBS="-lFestival")
  dnl GST_CHECK_LIBHEADER(FESTIVAL, Festival, , , festival/festival.h, FESTIVAL_LIBS="-lFestival")
  dnl AC_LANG_POP(C++)
  dnl AC_SUBST(FESTIVAL_LIBS)
dnl  HAVE_FESTIVAL=yes
dnl])

dnl *** FLAC ***
translit(dnm, m, l) AM_CONDITIONAL(USE_FLAC, true)
GST_CHECK_FEATURE(FLAC, [FLAC lossless audio], flacenc flacdec, [
  GST_CHECK_LIBHEADER(FLAC, FLAC, FLAC__seekable_stream_encoder_new, -lm, FLAC/all.h, FLAC_LIBS="-lFLAC")
  AC_SUBST(FLAC_LIBS)
])

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

dnl *** GDK pixbuf ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GDK_PIXBUF, true)
GST_CHECK_FEATURE(GDK_PIXBUF, [GDK pixbuf], gdkpixbufsrc, [
  if test $HAVE_GTK_22 = "yes"; then HAVE_GDK_PIXBUF=yes; fi;
])

dnl *** Gnome VFS ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GNOME_VFS, true)
GST_CHECK_FEATURE(GNOME_VFS, [Gnome VFS], gnomevfssrc, [
  PKG_CHECK_MODULES(GNOME_VFS, gnome-vfs-2.0, HAVE_GNOME_VFS="yes", HAVE_GNOME_VFS="no")
  AC_SUBST(GNOME_VFS_CFLAGS)
  AC_SUBST(GNOME_VFS_LIBS)
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

dnl *** Hermes ***
translit(dnm, m, l) AM_CONDITIONAL(USE_HERMES, true)
GST_CHECK_FEATURE(HERMES, [Hermes library], colorspace, [
  GST_CHECK_LIBHEADER(HERMES, Hermes, Hermes_ConverterInstance, , Hermes/Hermes.h, HERMES_LIBS="-lHermes")
], AC_SUBST(HERMES_LIBS))

dnl *** http ***
dnl translit(dnm, m, l) AM_CONDITIONAL(USE_HTTP, true)
dnl GST_CHECK_FEATURE(HTTP, [http plug-ins], gsthttpsrc, [
dnl  dnl FIXME: need to check for header
dnl  GHTTP_LIBS=
dnl  GST_HTTPSRC_GET_TYPE=
dnl  if test x$USE_GLIB2 = xyes; then
dnl    AC_MSG_WARN(ghttp disabled for glib2.0)
dnl  else
dnl    AC_CHECK_LIB(ghttp, ghttp_request_new,
dnl      [HTTP_LIBS="-lghttp"
dnl       GST_HTTPSRC_GET_TYPE="gst_httpsrc_get_type"
dnl       HAVE_HTTP=yes
dnl      ], :, $LIBS)
dnl  fi
dnl  AC_SUBST(HTTP_LIBS)
dnl  AC_SUBST(GST_HTTPSRC_GET_TYPE)
dnl ])

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
  PKG_CHECK_MODULES(JACK, jack >= 0.29.0, HAVE_JACK="yes", HAVE_JACK="no")
  AC_SUBST(JACK_CFLAGS)
  AC_SUBST(JACK_LIBS)
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

dnl *** lame ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LAME, true)
GST_CHECK_FEATURE(LAME, [lame mp3 encoder library], lame, [
  GST_CHECK_LIBHEADER(LAME, mp3lame, lame_init, -lm, lame/lame.h, HAVE_LAME="yes" LAME_LIBS="-lmp3lame")
])
AC_SUBST(LAME_LIBS)

dnl *** libcolorspace ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LCS, true)
GST_CHECK_FEATURE(LCS, Lcs, lcs, [
  PKG_CHECK_MODULES(LCS, lcs, HAVE_LCS="yes", HAVE_LCS="no")
  AC_SUBST(LCS_CFLAGS)
  AC_SUBST(LCS_LIBS)
])

dnl *** libdv ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBDV, true)
GST_CHECK_FEATURE(LIBDV, [libdv DV/video decoder], dvdec, [
  PKG_CHECK_MODULES(LIBDV, libdv >= 0.98, HAVE_LIBDV="yes", HAVE_LIBDV="no")
  AC_SUBST(LIBDV_CFLAGS)
  AC_SUBST(LIBDV_LIBS)
])

dnl *** libfame ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBFAME, true)
GST_CHECK_FEATURE(LIBFAME, [libfame MPEG1/4 encoder], libfame, [
  AM_PATH_LIBFAME(0.9.0, HAVE_LIBFAME="yes", HAVE_LIBFAME="no")
  AC_SUBST(LIBFAME_CFLAGS)
  AC_SUBST(LIBFAME_LIBS)
])

dnl *** libpng ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBPNG, true)
GST_CHECK_FEATURE(LIBPNG, [libpng PNG encoder], pngenc, [
  PKG_CHECK_MODULES(LIBPNG, libpng12, HAVE_LIBPNG="yes", HAVE_LIBPNG="no")
  AC_SUBST(LIBPNG_CFLAGS)
  AC_SUBST(LIBPNG_LIBS)
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
      save_libs=$LIBS
      LIBS="-lz"
      AC_CHECK_LIB(id3tag, id3_tag_options, HAVE_MAD="yes" MAD_LIBS="-lmad -lid3tag -lz")
      LIBS=$save_LIBS
    fi
  fi    
])
AC_SUBST(MAD_LIBS)

dnl *** mikmod ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MIKMOD, true)
GST_CHECK_FEATURE(MIKMOD, [mikmod plug-in], mikmod, [
  AM_PATH_LIBMIKMOD(, HAVE_MIKMOD=yes, HAVE_MIKMOD=no)
  AC_SUBST(MIKMOD_LIBS, "$LIBMIKMOD_LIBS")
  AC_SUBST(MIKMOD_CFLAGS, "$LIBMIKMODCFLAGS")
])

dnl *** mpeg2dec ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MPEG2DEC, true)
GST_CHECK_FEATURE(MPEG2DEC, [mpeg2dec], mpeg2dec, [
  PKG_CHECK_MODULES(MPEG2DEC, libmpeg2 >= 0.3.1,
      HAVE_MPEG2DEC="yes", HAVE_MPEG2DEC="no")
  AC_SUBST(MPEG2DEC_CFLAGS)
  AC_SUBST(MPEG2DEC_LIBS)
])

dnl *** mpeg2enc ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MPEG2ENC, true)
GST_CHECK_FEATURE(MPEG2ENC, [mpeg2enc], mpeg2enc, [
  PKG_CHECK_MODULES(MPEG2ENC, mjpegtools >= 1.6.1.92,
      HAVE_MPEG2ENC="yes", HAVE_MPEG2ENC="no")
  MPEG2ENC_LIBS="$MPEG2ENC_LIBS -lmpeg2encpp"
  AC_SUBST(MPEG2ENC_CFLAGS)
  AC_SUBST(MPEG2ENC_LIBS)
])

dnl *** mplex ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MPLEX, true)
GST_CHECK_FEATURE(MPLEX, [mplex], mplex, [HAVE_MPLEX=$HAVE_CXX])

dnl *** pango ***
translit(dnm, m, l) AM_CONDITIONAL(USE_PANGO, true)
GST_CHECK_FEATURE(PANGO, [pango], pango, [
  PKG_CHECK_MODULES(PANGO, pango pangoft2,
      HAVE_PANGO="yes", HAVE_PANGO="no")
  AC_SUBST(PANGO_CFLAGS)
  AC_SUBST(PANGO_LIBS)
])

dnl *** raw1394 ***
translit(dnm, m, l) AM_CONDITIONAL(USE_RAW1394, true)
GST_CHECK_FEATURE(RAW1394, [raw1394 library], dv1394src, [
  GST_CHECK_LIBHEADER(RAW1394, raw1394, raw1394_new_handle,, libraw1394/raw1394.h, RAW1394_LIBS="-lraw1394")
  AC_SUBST(RAW1394_LIBS)
])

dnl *** SDL ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SDL, true)
GST_CHECK_FEATURE(SDL, [SDL plug-in], sdlvideosink, [
 AM_PATH_SDL(, HAVE_SDL=yes, HAVE_SDL=no)
])

dnl *** shout ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SHOUT, true)
GST_CHECK_FEATURE(SHOUT, [shout plug-in], icecastsend, [
  GST_CHECK_LIBHEADER(SHOUT, shout, shout_init_connection,, shout/shout.h, SHOUT_LIBS="-lshout")
  AC_SUBST(SHOUT_LIBS)
])

dnl *** shout2 *** 
translit(dnm, m, l) AM_CONDITIONAL(USE_SHOUT2, true)
GST_CHECK_FEATURE(SHOUT2, [shout2 plug-in], shout2send, [
  AM_PATH_SHOUT2(HAVE_SHOUT2=yes, HAVE_SHOUT2=no)
  AC_SUBST(SHOUT2_CFLAGS)
  AC_SUBST(SHOUT2_LIBS)
])

dnl *** sidplay ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SIDPLAY, true)
GST_CHECK_FEATURE(SIDPLAY, [sidplay plug-in], sidplay, [
  GST_PATH_SIDPLAY()
])

dnl *** smoothwave ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SMOOTHWAVE, true)
GST_CHECK_FEATURE(SMOOTHWAVE, [smoothwave plug-in], smoothwave, [
  if test $HAVE_GTK = "yes"; then HAVE_SMOOTHWAVE=yes; fi;
])


dnl *** snapshot ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LIBPNG, true)
GST_CHECK_FEATURE(LIBPNG, [snapshot plug-in], snapshot, [
  GST_CHECK_LIBHEADER(LIBPNG, png, png_read_info, -lz -lm, png.h, LIBPNG_LIBS="-lpng -lz -lm")
  AC_SUBST(LIBPNG_LIBS)
])

dnl *** speex ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SPEEX, true)
GST_CHECK_FEATURE(SPEEX, [speex plug-in], speex, [
  GST_CHECK_LIBHEADER(SPEEX, speex, speex_bits_init, , speex.h, HAVE_SPEEX="yes" SPEEX_LIBS="-lspeex")
  AC_SUBST(SPEEX_CFLAGS)
  AC_SUBST(SPEEX_LIBS)
])

dnl *** sndfile ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SNDFILE, true)
GST_CHECK_FEATURE(SNDFILE, [sndfile plug-in], sfsrc sfsink, [
  PKG_CHECK_MODULES(SNDFILE, sndfile >= 1.0.0, HAVE_SNDFILE="yes", HAVE_SNDFILE="no")
  AC_SUBST(SNDFILE_CFLAGS)
  AC_SUBST(SNDFILE_LIBS)
])

dnl *** swfdec ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SWFDEC, true)
GST_CHECK_FEATURE(SWFDEC, [swfdec plug-in], swfdec, [
  PKG_CHECK_MODULES(SWFDEC, swfdec >= 0.1.3.1, HAVE_SWFDEC=yes, HAVE_SWFDEC=no)
  AC_SUBST(SWFDEC_CFLAGS)
  AC_SUBST(SWFDEC_LIBS)
])

dnl *** tarkin ***
dnl for now the sources are included in the plug-in
dnl and should be moved to ext-libs/ perhaps
translit(dnm, m, l) AM_CONDITIONAL(USE_TARKIN, true)
GST_CHECK_FEATURE(TARKIN, [tarkinenc tarkindec], tarkin, [
  HAVE_TARKIN="yes"
])

dnl *** ogg ***
translit(dnm, m, l) AM_CONDITIONAL(USE_OGG, true)
GST_CHECK_FEATURE(OGG, [ogg de/encoder], oggdemux oggmux, [
  XIPH_PATH_OGG(HAVE_OGG=yes, HAVE_OGG=no)
  AS_SCRUB_INCLUDE(OGG_CFLAGS)
])

dnl *** vorbis ***
dnl AM_PATH_VORBIS only takes two options
translit(dnm, m, l) AM_CONDITIONAL(USE_VORBIS, true)
GST_CHECK_FEATURE(VORBIS, [vorbis plug-in], vorbisenc vorbisdec, [
  XIPH_PATH_VORBIS(HAVE_VORBIS=yes, HAVE_VORBIS=no)
  AS_SCRUB_INCLUDE(VORBIS_CFLAGS)
])

dnl *** XVID ***
translit(dnm, m, l) AM_CONDITIONAL(USE_XVID, true)
GST_CHECK_FEATURE(XVID, [xvid plugins], xvid, [
  HAVE_XVID=yes
  AC_CHECK_HEADER(xvid.h, ,
                  [ AC_MSG_WARN([Xvid headers not found]) &&
                    HAVE_XVID=no ] )
  LIBS="-lm"
  AC_CHECK_LIB(xvidcore, xvid_encore, ,
               [ AC_MSG_WARN([Xvid encore libs not found]) &&
                 HAVE_XVID=no ] )
  AC_CHECK_LIB(xvidcore, xvid_decore, ,
               [ AC_MSG_WARN([Xvid decore libs not found]) &&
                 HAVE_XVID=no ] )
  if test x$HAVE_XVID = xyes; then
    XVID_LIBS="-lxvidcore -lm"
    AC_SUBST(XVID_LIBS)
  fi
])


fi dnl of EXT plugins

dnl Check for atomic.h
dnl Note: use AC_CHECK_HEADER not AC_CHECK_HEADERS, because the latter
dnl defines the wrong default symbol as well (HAVE_ASM_ATOMIC_H)
AC_CHECK_HEADER(asm/atomic.h, HAVE_ATOMIC_H=yes, HAVE_ATOMIC_H=no)
dnl Do a compile to check that it has atomic_set (eg, linux 2.0 didn't)
if test x$HAVE_ATOMIC_H = xyes; then
  AC_TRY_RUN([
#include "asm/atomic.h"
main() { atomic_t t; atomic_set(&t,0); atomic_inc(&t); atomic_add(1,&t);return 0;}
  ],, [
    # Not successful
    if test x$HAVE_ATOMIC_H = xyes; then
      AC_MSG_WARN(Atomic reference counting is out of date: doing without.)
    fi
    HAVE_ATOMIC_H=no
  ], [
    # Cross compiling
    AC_MSG_RESULT(yes)
    AC_MSG_WARN(Can't check properly for atomic reference counting.  Assuming OK.)
  ])
fi


dnl ######################################################################
dnl # Check command line parameters, and set shell variables accordingly #
dnl ######################################################################

AC_ARG_ENABLE(libmmx,
  AC_HELP_STRING([--enable-libmmx],[use libmmx, if available]),
[case "${enableval}" in
  yes) USE_LIBMMX=$HAVE_LIBMMX ;;
  no)  USE_LIBMMX=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-libmmx) ;;
esac], 
[USE_LIBMMX=$HAVE_LIBMMX]) dnl Default value

AC_ARG_ENABLE(atomic,
  AC_HELP_STRING([--enable-atomic],[use atomic reference counting header]),
[case "${enableval}" in
  yes) USE_ATOMIC_H=$HAVE_ATOMIC_H;;
  noset) USE_ATOMIC_H=$HAVE_ATOMIC_H;;
  no)  USE_ATOMIC_H=no;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-atomic) ;;
esac], 
[USE_ATOMIC_H=$HAVE_ATOMIC_H]) dnl Default value

AC_ARG_ENABLE(profiling,
  AC_HELP_STRING([--enable-profiling],
                 [-pg to compiler commandline, for profiling]),
[case "${enableval}" in
  yes) USE_PROFILING=yes ;;
  no)  UES_PROFILING=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-profiling) ;;
esac], 
[USE_PROFILING=no]) dnl Default value

AC_ARG_ENABLE(tests,
  AC_HELP_STRING([--disable-tests],[disable building test apps]),
[case "${enableval}" in
  yes) BUILD_TESTS=yes ;;
  no)  BUILD_TESTS=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-tests) ;;
esac], 
[BUILD_TESTS=yes]) dnl Default value

AC_ARG_ENABLE(examples,
  AC_HELP_STRING([--disable-examples],[disable building examples]),
[case "${enableval}" in
  yes) BUILD_EXAMPLES=yes ;;
  no)  BUILD_EXAMPLES=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-examples) ;;
esac], 
[BUILD_EXAMPLES=yes]) dnl Default value

dnl seeking needs freetype, so check for it here
AC_CHECK_FT2(2.0.9,HAVE_FT2=yes,HAVE_FT2=no)
dnl make the HAVE_FT2 variable available to automake and Makefile.am
AM_CONDITIONAL(HAVE_FT2, test "x$HAVE_FT2" = "xyes")
AC_SUBST(FT2_CFLAGS)
AC_SUBST(FT2_LIBS)

dnl ################################################
dnl # Set defines according to variables set above #
dnl ################################################


if test "x$USE_LIBMMX" = xyes; then
  AC_DEFINE(HAVE_LIBMMX, 1, [Define if libmmx is available])
fi

if test "x$USE_ATOMIC_H" = xyes; then
  AC_DEFINE(HAVE_ATOMIC_H, 1, [Define if atomic.h header file is available])
fi

# do not use deprecated stuff
GST_CFLAGS="$GST_CFLAGS -DGST_DISABLE_DEPRECATED"

if test "x$USE_DEBUG" = xyes; then
  GST_CFLAGS="$GST_CFLAGS -g"
fi

dnl #############################
dnl # Set automake conditionals #
dnl #############################

dnl These should be "USE_*" instead of "HAVE_*", but some packages expect
dnl HAVE_ and it is likely to be easier to stick with the old name
AM_CONDITIONAL(HAVE_LIBMMX,         test "x$USE_LIBMMX" = "xyes")

AM_CONDITIONAL(HAVE_ATOMIC_H,       test "x$USE_ATOMIC_H" = "xyes")

AM_CONDITIONAL(EXPERIMENTAL,        test "$EXPERIMENTAL" = "$xyes")
AM_CONDITIONAL(BROKEN,              test "$BROKEN" = "$xyes")

AM_CONDITIONAL(HAVE_NASM,           test "x$HAVE_NASM" = "xyes")
AM_CONDITIONAL(HAVE_GTK,            test "x$HAVE_GTK" = "xyes")
AM_CONDITIONAL(HAVE_GTK_DOC,        $HAVE_GTK_DOC)
AM_CONDITIONAL(BUILD_DOCS,          test "x$BUILD_DOCS" = "xyes")
AM_CONDITIONAL(BUILD_TESTS,         test "x$BUILD_TESTS" = "xyes")
AM_CONDITIONAL(BUILD_EXAMPLES,      test "x$BUILD_EXAMPLES" = "xyes")
AM_CONDITIONAL(BUILD_PLUGIN_DOCS,   test "x$BUILD_PLUGIN_DOCS" = "xyes")
AM_CONDITIONAL(HAVE_FIG2DEV_PNG,    $HAVE_FIG2DEV_PNG)
AM_CONDITIONAL(HAVE_FIG2DEV_PDF,    $HAVE_FIG2DEV_PDF)
AM_CONDITIONAL(HAVE_RAW1394,        test "x$HAVE_RAW1394" = "xyes")

dnl prefer internal headers to already installed ones
GST_CFLAGS="-I\$(top_srcdir)/gst-libs $GST_CFLAGS $GST_ERROR"

AC_SUBST(GST_LIBS)
AC_SUBST(GST_CFLAGS)

dnl ###########################
dnl # Configure external libs #
dnl ###########################
if test "x$HAVE_FFMPEG" = xyes; then
  AC_CONFIG_SUBDIRS(gst-libs/ext/ffmpeg/ffmpeg)
fi 

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
[GST_PACKAGE="GStreamer"]) dnl Default value
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
[GST_ORIGIN="http://gstreamer.net"]) dnl Default value
AC_MSG_NOTICE(Using $GST_ORIGIN as package origin)
AC_DEFINE_UNQUOTED(GST_ORIGIN, "$GST_ORIGIN", [package origin])

dnl #########################
dnl # Make the output files #
dnl #########################

AC_CONFIG_FILES(
Makefile
pkgconfig/gstreamer-libs.pc
pkgconfig/gstreamer-libs-uninstalled.pc
pkgconfig/gstreamer-play.pc
pkgconfig/gstreamer-play-uninstalled.pc
pkgconfig/gstreamer-interfaces.pc
pkgconfig/gstreamer-interfaces-uninstalled.pc
gst-libs/gst/gconf/gstreamer-gconf.pc
gst-libs/gst/gconf/gstreamer-gconf-uninstalled.pc
gst-plugins.spec
gst/Makefile
gst/ac3parse/Makefile
gst/adder/Makefile
gst/audioconvert/Makefile
gst/audioscale/Makefile
gst/auparse/Makefile
gst/avi/Makefile
gst/asfdemux/Makefile
gst/cdxaparse/Makefile
gst/chart/Makefile
gst/cutter/Makefile
gst/debug/Makefile
gst/deinterlace/Makefile
gst/effectv/Makefile
gst/festival/Makefile
gst/filter/Makefile
gst/flx/Makefile
gst/goom/Makefile
gst/id3/Makefile
gst/intfloat/Makefile
gst/law/Makefile
gst/level/Makefile
gst/matroska/Makefile
gst/median/Makefile
gst/mixmatrix/Makefile
gst/mpeg1sys/Makefile
gst/mpeg1videoparse/Makefile
gst/mpeg2sub/Makefile
gst/mpegaudio/Makefile
gst/mpegaudioparse/Makefile
gst/mpegstream/Makefile
gst/modplug/Makefile
gst/modplug/libmodplug/Makefile
gst/monoscope/Makefile
gst/oneton/Makefile
gst/overlay/Makefile
gst/passthrough/Makefile
gst/playondemand/Makefile
gst/qtdemux/Makefile
gst/realmedia/Makefile
gst/rtjpeg/Makefile
gst/rtp/Makefile
gst/silence/Makefile
gst/sine/Makefile
gst/smooth/Makefile
gst/smpte/Makefile
gst/spectrum/Makefile
gst/speed/Makefile
gst/stereo/Makefile
gst/switch/Makefile
gst/synaesthesia/Makefile
gst/tags/Makefile
gst/tcp/Makefile
gst/typefind/Makefile
gst/udp/Makefile
gst/vbidec/Makefile
gst/videocrop/Makefile
gst/videodrop/Makefile
gst/videofilter/Makefile
gst/videoflip/Makefile
gst/videoscale/Makefile
gst/videotestsrc/Makefile
gst/volenv/Makefile
gst/volume/Makefile
gst/wavenc/Makefile
gst/wavparse/Makefile
gst/y4m/Makefile
sys/Makefile
sys/cdrom/Makefile
sys/dxr3/Makefile
sys/glsink/Makefile
sys/oss/Makefile
sys/qcam/Makefile
sys/v4l/Makefile
sys/v4l2/Makefile
sys/vcd/Makefile
sys/ximage/Makefile
sys/xvimage/Makefile
sys/xvideo/Makefile
ext/Makefile
ext/a52dec/Makefile
ext/aalib/Makefile
ext/alsa/Makefile
ext/arts/Makefile
ext/artsd/Makefile
ext/audiofile/Makefile
ext/cdparanoia/Makefile
ext/divx/Makefile
ext/dv/Makefile
ext/dvdread/Makefile
ext/dvdnav/Makefile
ext/esd/Makefile
ext/faac/Makefile
ext/faad/Makefile
ext/ffmpeg/Makefile
ext/flac/Makefile
ext/gdk_pixbuf/Makefile
ext/gnomevfs/Makefile
ext/gsm/Makefile
ext/hermes/Makefile
dnl ext/http/Makefile
ext/jack/Makefile
ext/jpeg/Makefile
ext/ladspa/Makefile
ext/lame/Makefile
ext/ivorbis/Makefile
ext/lcs/Makefile
ext/libfame/Makefile
ext/libpng/Makefile
ext/mad/Makefile
ext/mas/Makefile
ext/mikmod/Makefile
ext/mpeg2dec/Makefile
ext/mpeg2enc/Makefile
ext/mplex/Makefile
ext/ogg/Makefile
ext/pango/Makefile
ext/raw1394/Makefile
ext/sdl/Makefile
ext/shout/Makefile
ext/shout2/Makefile
ext/sidplay/Makefile
ext/smoothwave/Makefile
ext/snapshot/Makefile
ext/speex/Makefile
ext/sndfile/Makefile
ext/swfdec/Makefile
ext/vorbis/Makefile
ext/tarkin/Makefile
ext/xvid/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/audio/Makefile
gst-libs/gst/colorbalance/Makefile
gst-libs/gst/floatcast/Makefile
gst-libs/gst/gconf/Makefile
gst-libs/gst/idct/Makefile
gst-libs/gst/media-info/Makefile
gst-libs/gst/mixer/Makefile
gst-libs/gst/navigation/Makefile
gst-libs/gst/play/Makefile
gst-libs/gst/propertyprobe/Makefile
gst-libs/gst/resample/Makefile
gst-libs/gst/riff/Makefile
gst-libs/gst/tag/Makefile
gst-libs/gst/tuner/Makefile
gst-libs/gst/video/Makefile
gst-libs/gst/xoverlay/Makefile
gst-libs/gst/xwindowlistener/Makefile
gst-libs/ext/Makefile
gst-libs/ext/ffmpeg/Makefile
gst-libs/ext/mplex/Makefile
examples/dynparams/Makefile
examples/capsfilter/Makefile
examples/seeking/Makefile
examples/indexing/Makefile
examples/gstplay/Makefile
examples/Makefile
testsuite/spider/Makefile
testsuite/alsa/Makefile
testsuite/Makefile
tools/Makefile
tools/gst-launch-ext
gconf/Makefile
pkgconfig/Makefile
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
