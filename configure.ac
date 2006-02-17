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
AS_VERSION(gst-plugins-ugly, GST_PLUGINS_UGLY_VERSION, 0, 10, 1, 2,
  GST_CVS="no", GST_CVS="yes")

AM_INIT_AUTOMAKE($PACKAGE,$VERSION)

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$GST_PLUGINS_UGLY_VERSION_MAJOR.$GST_PLUGINS_UGLY_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)

AS_LIBTOOL_TAGS([CXX])
AM_PROG_LIBTOOL

dnl *** required versions of GStreamer stuff ***
GST_REQ=0.10.3
GSTPB_REQ=0.10.3

AC_CONFIG_SRCDIR([ext/mad/gstmad.c])
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
                                                                            
dnl GETTEXT_PACKAGE=gst-plugins-ugly-$GST_MAJORMINOR
dnl AC_SUBST(GETTEXT_PACKAGE)
dnl AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], "$GETTEXT_PACKAGE",
dnl                   [gettext package name])

dnl define LOCALEDIR in config.h
dnl AS_AC_EXPAND(LOCALEDIR, $datadir/locale)
dnl AC_DEFINE_UNQUOTED([LOCALEDIR], "$LOCALEDIR",
dnl                   [gettext locale dir])

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

dnl used by ext/a52dec
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

dnl check for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
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
  gstreamer-plugins-base-$GST_MAJORMINOR >= $GSTPB_REQ,
  HAVE_GST_PLUGINS_BASE="yes", HAVE_GST_PLUGINS_BASE="no")

if test "x$HAVE_GST_PLUGINS_BASE" = "xno"; then
  AC_MSG_ERROR(no GStreamer Base Plugins development files found)
fi

AC_SUBST(GST_PLUGINS_BASE_LIBS)
AC_SUBST(GST_PLUGINS_BASE_CFLAGS)

dnl Determine endianness
AC_C_BIGENDIAN

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
  AC_DEFINE_UNQUOTED(HAVE_LIBOIL, 1, [Define if liboil is being used])
  true
fi

dnl ===========================================================================
dnl ============================= gst plug-ins ================================
dnl ===========================================================================

plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"
AC_SUBST(plugindir)

GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '[_]*(gst_|Gst|GST_).*' $GST_LIBS -no-undefined"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl these are all the gst plug-ins, compilable without additional libs
GST_PLUGINS_ALL="\
		asfdemux \
		dvdlpcmdec \
        iec958 \
		mpegaudioparse \
        mpegstream \
		realmedia \
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

dnl ###########################
dnl # Configure external libs #
dnl ###########################

dnl *** a52dec ***
translit(dnm, m, l) AM_CONDITIONAL(USE_A52DEC, true)
GST_CHECK_FEATURE(A52DEC, [a52dec], a52dec, [
  AC_CHECK_A52DEC(HAVE_A52DEC=yes, HAVE_A52DEC=no)
])

dnl *** AMR-NB ***
translit(dnm, m, l) AM_CONDITIONAL(USE_AMRNB, true)
GST_CHECK_FEATURE(AMRNB, [AMR-NB], amrnbdec amrnbenc, [
  GST_CHECK_LIBHEADER(AMRNB, amrnb,
		      Decoder_Interface_init, -lm,
		      amrnb/interf_dec.h,
		      AMRNB_LIBS="-lamrnb -lm"
		      AC_SUBST(AMRNB_LIBS))
])

dnl *** dvdread ***
translit(dnm, m, l) AM_CONDITIONAL(USE_DVDREAD, true)
GST_CHECK_FEATURE(DVDREAD, [dvdread library], dvdreadsrc, [
  GST_CHECK_LIBHEADER(DVDREAD, dvdread, DVDOpen, , dvdread/dvd_reader.h, DVDREAD_LIBS="-ldvdread")
  AC_SUBST(DVDREAD_LIBS)
])

dnl *** lame ***
translit(dnm, m, l) AM_CONDITIONAL(USE_LAME, true)
GST_CHECK_FEATURE(LAME, [lame mp3 encoder library], lame, [
  GST_CHECK_LIBHEADER(LAME, mp3lame, lame_init, -lm, lame/lame.h,
  [
    HAVE_LAME="yes"
    LAME_LIBS="-lmp3lame -lm"
    dnl is lame presets available
    LAME_CFLAGS=""
    AC_TRY_RUN([
#include <lame/lame.h>
int main (int argc, char *argv[])
{
  printf("%d\n", MEDIUM);
  return 0;
}
      ],
      [LAME_CFLAGS="-DGSTLAME_PRESET"],
      [LAME_CFLAGS=""]
    )
  AC_SUBST(LAME_CFLAGS)
  AC_SUBST(LAME_LIBS)
  ])
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

dnl *** mpeg2dec ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MPEG2DEC, true)
GST_CHECK_FEATURE(MPEG2DEC, [mpeg2dec], mpeg2dec, [
  PKG_CHECK_MODULES(MPEG2DEC, libmpeg2 >= 0.4.0,
      HAVE_MPEG2DEC="yes", HAVE_MPEG2DEC="no")
  AC_SUBST(MPEG2DEC_CFLAGS)
  AC_SUBST(MPEG2DEC_LIBS)
])

dnl *** sidplay : works with libsidplay 1.36.x (not 2.x.x) ***
translit(dnm, m, l) AM_CONDITIONAL(USE_SIDPLAY, true)
GST_CHECK_FEATURE(SIDPLAY, [sidplay plug-in], sidplay, [
  GST_PATH_SIDPLAY()
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

dnl po/Makefile.in

AC_CONFIG_FILES(
Makefile
gst/Makefile
gst/asfdemux/Makefile
gst/dvdlpcmdec/Makefile
gst/iec958/Makefile
gst/mpegaudioparse/Makefile
gst/mpegstream/Makefile
gst/realmedia/Makefile
ext/Makefile
ext/a52dec/Makefile
ext/amrnb/Makefile
ext/dvdread/Makefile
ext/lame/Makefile
ext/mad/Makefile
ext/mpeg2dec/Makefile
ext/sidplay/Makefile
docs/Makefile
docs/plugins/Makefile
docs/version.entities
common/Makefile
common/m4/Makefile
m4/Makefile
gst-plugins-ugly.spec
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
