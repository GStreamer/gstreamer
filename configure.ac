AC_PREREQ(2.52)

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT(GStreamer Ugly Plug-ins, 0.10.3.1,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gst-plugins-ugly)

dnl initialize automake
AM_INIT_AUTOMAKE

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_CVS="no", GST_CVS="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([ext/mad/gstmad.c])

dnl define the output header for config
AM_CONFIG_HEADER(config.h)

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl sets host_* variables
AC_CANONICAL_HOST

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)

dnl FIXME: this macro doesn't actually work;
dnl the generated libtool script has no support for the listed tags.
dnl So this needs to be fixed first if we want to use this
dnl AS_LIBTOOL_TAGS([CXX])

AM_PROG_LIBTOOL

dnl *** required versions of GStreamer stuff ***
GST_REQ=0.10.3
GSTPB_REQ=0.10.3

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl set up gettext
dnl AM_GNU_GETTEXT_VERSION(0.11.5)
dnl AM_GNU_GETTEXT([external])
dnl GST_GETTEXT([gst-plugins-ugly-$GST_MAJOR_MINOR])

dnl *** check for arguments to configure ***

GST_ARG_DEBUG
GST_ARG_PROFILING
GST_ARG_VALGRIND
GST_ARG_GCOV

GST_ARG_EXAMPLES

GST_ARG_WITH_PKG_CONFIG_PATH
GST_ARG_WITH_PACKAGE_NAME
GST_ARG_WITH_PACKAGE_ORIGIN

dnl these are all the gst plug-ins, compilable without additional libs
GST_PLUGINS_ALL="\
	asfdemux \
	dvdlpcmdec \
	dvdsub \
        iec958 \
	mpegaudioparse \
        mpegstream \
	realmedia \
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

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for documentation tools
GTK_DOC_CHECK([1.3])
AS_PATH_PYTHON([2.1])

dnl *** checks for libraries ***

dnl *** checks for header files ***

dnl used by ext/a52dec
AX_CREATE_STDINT_H

dnl Check for malloc.h
AC_CHECK_HEADERS([malloc.h])

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***

dnl Check for a way to display the function name in debug output
GST_CHECK_FUNCTION

dnl *** checks for dependancy libraries ***

dnl liboil is required
PKG_CHECK_MODULES(LIBOIL, liboil-0.3 >= 0.3.6, HAVE_LIBOIL=yes, HAVE_LIBOIL=no)
if test "x$HAVE_LIBOIL" != "xyes"
then
  AC_ERROR([liboil-0.3.6 or later is required])
fi

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
GST_CHECK_GST($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_BASE($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_CHECK($GST_MAJORMINOR, [$GST_REQ], no)
GST_CHECK_GST_PLUGINS_BASE($GST_MAJORMINOR, [$GSTPB_REQ])

dnl FIXME: get rid of this by making sure gstreamer-check brings it in
dnl check for "check", unit testing library/header
AM_PATH_CHECK(0.9.2, HAVE_CHECK=yes, HAVE_CHECK=no)
AM_CONDITIONAL(HAVE_CHECK, test "x$HAVE_CHECK" = "xyes")

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

dnl *** ext plug-ins ***
dnl keep this list sorted alphabetically !

if test "x$BUILD_EXTERNAL" = "xyes"; then

echo
AC_MSG_NOTICE([Checking libraries for plugins in ext/])
echo

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
  dnl now check for dvdread/nav_print.h - see #133002
  AC_CHECK_HEADER(dvdread/nav_print.h, , [
      AC_MSG_WARN([header dvdread/nav_print.h from dvdread missing])
      HAVE_DVDNAV="no"
  ])
  AS_SCRUB_INCLUDE(DVDNAV_CFLAGS)

  dnl disabled until plugin is fully functional
  HAVE_DVDNAV=no
], disabled, , [
  AC_MSG_NOTICE([dvdnavsrc not stable yet, not building])
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

dnl *** id3tag from the MAD project ***
translit(dnm, m, l) AM_CONDITIONAL(USE_ID3TAG, true)
GST_CHECK_FEATURE(ID3TAG, [id3tag reading and writing from the MAD project],
  id3tag, [
  dnl check with pkg-config first
  PKG_CHECK_MODULES(ID3TAG, id3tag >= 0.15, HAVE_ID3TAG="yes", HAVE_ID3TAG="no")
  if test "x$HAVE_ID3TAG" = "xno"; then
    dnl fall back to oldskool detection
    AC_CHECK_HEADER(id3tag.h, [
      save_LIBS=$LIBS
      LIBS="-lz"
      AC_CHECK_LIB(id3tag, id3_tag_options,
        HAVE_ID3TAG="yes" ID3TAG_LIBS="-lid3tag -lz")
      LIBS=$save_LIBS
    ])
  fi
])
AC_SUBST(ID3TAG_LIBS)

dnl *** mad ***
translit(dnm, m, l) AM_CONDITIONAL(USE_MAD, true)
GST_CHECK_FEATURE(MAD, [mad mp3 decoder], mad, [
  if test "x$HAVE_ID3TAG" = "xyes"; then
    dnl check with pkg-config first
    PKG_CHECK_MODULES(MAD, mad >= 0.15 id3tag >= 0.15,
      HAVE_MAD="yes", HAVE_MAD="no")
    if test "x$HAVE_MAD" = "xno"; then
      dnl fall back to oldskool detection
      AC_CHECK_HEADER(mad.h, [
        AC_CHECK_LIB(mad, mad_decoder_finish,
          HAVE_MAD="yes" MAD_LIBS="-lmad $ID3TAG_LIBS")
      ])
    fi    
  else
    HAVE_MAD="no"
    AC_MSG_WARN([libid3tag not available, cannot build MAD MP3 decoder plugin])
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

fi dnl of EXT plugins

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

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_desc\$\$' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl *** output files ***

dnl po/Makefile.in

AC_CONFIG_FILES(
Makefile
gst/Makefile
gst/asfdemux/Makefile
gst/dvdlpcmdec/Makefile
gst/dvdsub/Makefile
gst/iec958/Makefile
gst/mpegaudioparse/Makefile
gst/mpegstream/Makefile
gst/realmedia/Makefile
ext/Makefile
ext/a52dec/Makefile
ext/amrnb/Makefile
ext/dvdnav/Makefile
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

GST_OUTPUT_PLUGINS
