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
AS_VERSION(gst-plugins-base, GST_PLUGINS_VERSION, 0, 9, 1, 1, GST_CVS="no", GST_CVS="yes")

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
AC_CONFIG_SRCDIR([gst/sine/gstsinesrc.c])
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
                                                                                
GETTEXT_PACKAGE=gst-plugins-base-$GST_MAJORMINOR
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
GST_REQ=0.9.0
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

PKG_CHECK_MODULES(GST_BASE, gstreamer-base-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_BASE="yes", HAVE_GST_BASE="no")

if test "x$HAVE_GST_BASE" = "xno"; then
  AC_MSG_ERROR(no libgstbase found)
fi

AC_SUBST(GST_BASE_LIBS)

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

# gdk_pixbuf gstreamer loader is considered experimental, so disable
# by default
if test "x$HAVE_GTK_22" = "xyes"; then
  HAVE_GDK_LOADERS=yes
else
  HAVE_GDK_LOADERS=no
fi

# we set the defaults always to make sure we have non-empty variables
# for the Makefile

# by default, stick to prefix
GDK_PIXBUF_LOADER_DIR=${libdir}/gtk-2.0/\${GTK_VERSION}/loaders
GDK_PIXBUF_CONF_DIR=${sysconfdir}/gtk-2.0


AC_ARG_ENABLE(gdk-pixbuf-loader,
  AC_HELP_STRING([--enable-gdk-pixbuf-loader],
              [whether to enable building of gdk_pixbuf loader]),
              :, HAVE_GDK_LOADERS="no")

if test "x$HAVE_GDK_LOADERS" = "xyes"; then
  AC_PATH_PROG(QUERYLOADERS, gdk-pixbuf-query-loaders, no)
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
     ]
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
     ]
  )
  AS_AC_EXPAND(GDK_PIXBUF_CONF_DIR, $GDK_PIXBUF_CONF_DIR)
  AC_SUBST(GDK_PIXBUF_CONF_DIR)
  AC_MSG_NOTICE([Putting GTK+-2 pixbuf loader config in $GDK_PIXBUF_CONF_DIR])
fi
AM_CONDITIONAL(HAVE_GDK_LOADERS, test "x$HAVE_GDK_LOADERS" = "xyes")

PKG_CHECK_MODULES(LIBOIL, liboil-0.3 >= 0.3.2, HAVE_LIBOIL=yes, HAVE_LIBOIL=no)
AC_SUBST(LIBOIL_CFLAGS)
AC_SUBST(LIBOIL_LIBS)
if test "x${HAVE_LIBOIL}" != xyes ; then
  AC_ERROR([liboil-0.3 is required])
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
	adder \
	audioconvert \
	audioscale \
        audiorate \
	ffmpegcolorspace \
	playback \
	sine \
        subparse \
	typefind \
	videotestsrc \
	videorate \
	videoscale \
        volume \
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

echo
AC_MSG_NOTICE([Checking libraries for plugins in sys/])
echo

dnl Check for X11
translit(dnm, m, l) AM_CONDITIONAL(USE_X, true)
GST_CHECK_FEATURE(X, [X libraries and plugins],
                  [ximagesink], [
  AC_PATH_XTRA
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

dnl *** Video 4 Linux 2 ***
dnl for information about the header/define, see sys/v4l2/gstv4l2element.h
dnl renamed to GST_V4L2 because of some conflict with kernel headers
translit(dnm, m, l) AM_CONDITIONAL(USE_GST_V4L2, true)
GST_CHECK_FEATURE(GST_V4L2, [Video 4 Linux 2], v4l2src, [
  HAVE_GST_V4L2="no"
  if test "$HAVE_X" = "yes"
  then
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
    ], [ HAVE_GST_V4L2="yes" && AC_MSG_RESULT(yes)],
       [ HAVE_GST_V4L2="no"  && AC_MSG_RESULT(no) &&
         AC_CHECK_HEADER(linux/videodev2.h,
                         [ AC_MSG_WARN([video4linux2 headers were found, but they're old. Please update v4l2 to compile the v4l2 plugins])],
                         [ AC_MSG_WARN([video4linux2 was not found])])
       ])
  fi
  dnl check for missing v4l2_buffer declaration (see #135919)
  if [ test x$HAVE_GST_V4L2 = xyes ]; then
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
  fi
])

dnl Next, check for the optional libraries:
dnl These are all libraries used in building plug-ins
dnl ================================================
dnl let's try and sort them alphabetically, shall we ?

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

dnl *** Gnome VFS ***
translit(dnm, m, l) AM_CONDITIONAL(USE_GNOME_VFS, true)
GST_CHECK_FEATURE(GNOME_VFS, [Gnome VFS], gnomevfssrc, [
  PKG_CHECK_MODULES(GNOME_VFS, gnome-vfs-2.0, HAVE_GNOME_VFS="yes", HAVE_GNOME_VFS="no")
  AC_SUBST(GNOME_VFS_CFLAGS)
  AC_SUBST(GNOME_VFS_LIBS)
])

dnl *** libvisual ***
USE_LIBVISUAL=no
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
PKG_CHECK_MODULES(FT2, freetype2 >= 2.0.9, HAVE_FT2="yes", [
  AC_CHECK_FT2(2.0.9, HAVE_FT2="yes", HAVE_FT2="no")
])
dnl make the HAVE_FT2 variable available to automake and Makefile.am
AM_CONDITIONAL(HAVE_FT2, test "x$HAVE_FT2" = "xyes")
AC_SUBST(FT2_CFLAGS)
AC_SUBST(FT2_LIBS)

dnl ################################################
dnl # Set defines according to variables set above #
dnl ################################################


if test "x$USE_ATOMIC_H" = xyes; then
  AC_DEFINE(HAVE_ATOMIC_H, 1, [Define if atomic.h header file is available])
fi

# do not use deprecated stuff
GST_CFLAGS="$GST_CFLAGS -DGST_DISABLE_DEPRECATED"

if test "x$USE_DEBUG" = xyes; then
  GST_CFLAGS="$GST_CFLAGS -g"
fi

dnl ######################
dnl # Checks for gtk-doc #
dnl ######################

GTK_DOC_CHECK([1.3])

dnl #############################
dnl # Set automake conditionals #
dnl #############################

dnl These should be "USE_*" instead of "HAVE_*", but some packages expect
dnl HAVE_ and it is likely to be easier to stick with the old name
AM_CONDITIONAL(HAVE_ATOMIC_H,       test "x$USE_ATOMIC_H" = "xyes")

AM_CONDITIONAL(EXPERIMENTAL,        test "$EXPERIMENTAL" = "$xyes")
AM_CONDITIONAL(BROKEN,              test "$BROKEN" = "$xyes")

AM_CONDITIONAL(HAVE_NASM,           test "x$HAVE_NASM" = "xyes")
AM_CONDITIONAL(HAVE_GTK,            test "x$HAVE_GTK" = "xyes")
AM_CONDITIONAL(BUILD_TESTS,         test "x$BUILD_TESTS" = "xyes")
AM_CONDITIONAL(BUILD_EXAMPLES,      test "x$BUILD_EXAMPLES" = "xyes")
AM_CONDITIONAL(HAVE_FIG2DEV_PNG,    $HAVE_FIG2DEV_PNG)
AM_CONDITIONAL(HAVE_FIG2DEV_PDF,    $HAVE_FIG2DEV_PDF)
AM_CONDITIONAL(HAVE_RAW1394,        test "x$HAVE_RAW1394" = "xyes")

dnl prefer internal headers to already installed ones
dnl also add builddir include for enumtypes and marshal
GST_CFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs $GST_CFLAGS $GST_ERROR"

AC_SUBST(GST_LIBS)
AC_SUBST(GST_CFLAGS)

dnl ###########################
dnl # Configure external libs #
dnl ###########################

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
gst-plugins-base.spec
gst/Makefile
gst/adder/Makefile
gst/audioconvert/Makefile
gst/audioscale/Makefile
gst/audiorate/Makefile
gst/ffmpegcolorspace/Makefile
gst/playback/Makefile
gst/sine/Makefile
gst/subparse/Makefile
gst/typefind/Makefile
gst/videotestsrc/Makefile
gst/videorate/Makefile
gst/videoscale/Makefile
gst/volume/Makefile
sys/Makefile
sys/ximage/Makefile
sys/xvimage/Makefile
ext/Makefile
ext/alsa/Makefile
ext/cdparanoia/Makefile
ext/gnomevfs/Makefile
ext/libvisual/Makefile
ext/ogg/Makefile
ext/theora/Makefile
ext/vorbis/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/audio/Makefile
gst-libs/gst/floatcast/Makefile
gst-libs/gst/gconf/Makefile
gst-libs/gst/interfaces/Makefile
gst-libs/gst/net/Makefile
gst-libs/gst/riff/Makefile
gst-libs/gst/tag/Makefile
gst-libs/gst/video/Makefile
gst-libs/ext/Makefile
examples/seeking/Makefile
examples/Makefile
tools/Makefile
gconf/Makefile
gconf/gstreamer.schemas
pkgconfig/Makefile
pkgconfig/gstreamer-gconf.pc
pkgconfig/gstreamer-gconf-uninstalled.pc
pkgconfig/gstreamer-plugins-base.pc
pkgconfig/gstreamer-plugins-base-uninstalled.pc
docs/Makefile
docs/libs/Makefile
docs/version.entities
po/Makefile.in
common/Makefile
common/m4/Makefile
m4/Makefile
)
AC_OUTPUT

echo -n "configure: *** Plug-ins that will be built:"
echo -e "$GST_PLUGINS_YES" | sort
echo
echo -n "configure: *** Plug-ins that will not be built:"
echo -e "$GST_PLUGINS_NO" | sort
echo
if test "x$BUILD_EXTERNAL" = "xno"; then
  echo "configure: *** No external plug-ins will be built"
fi
