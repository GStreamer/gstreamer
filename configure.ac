AC_INIT
AC_CANONICAL_TARGET([])

dnl when going to/from release please set the nano (fourth number) right !
AS_VERSION(gstreamer, GST_VERSION, 0, 3, 2, 3)
AM_INIT_AUTOMAKE($PACKAGE,$VERSION)
AS_LIBTOOL(GST, 0, 0, 0, yes)

AC_CONFIG_SRCDIR([gst/gst.c])
AM_CONFIG_HEADER(config.h)

dnl Add parameters for aclocal
dnl (This must come after AM_INIT_AUTOMAKE, since it modifies ACLOCAL)
ACLOCAL="$ACLOCAL -I common/m4 $ACLOCAL_FLAGS"

AM_MAINTAINER_MODE

AC_PROG_CC
AM_PROG_CC_STDC
AM_PROG_AS
AS="${CC}"
AC_PROG_CXX
AC_PROG_CXXCPP
AC_ISC_POSIX

dnl We disable static building for development, for time savings
dnl *NOTE*: dnl this line before release, so release does static too
dnl AM_DISABLE_STATIC

AC_HEADER_STDC([])


dnl ##############################
dnl # Do automated configuration #
dnl ##############################

dnl Check for tools:
dnl ================

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
else AC_DEFINE(HAVE_NASM, 1, [Define if NASM, the netwide assembler, is available])
  HAVE_NASM="yes"
fi

GST_DOC()
GST_ARCH()

dnl
dnl We should really use AC_SYS_LARGEFILE, but the problem is
dnl many of the plugins don't include "config.h".  To assure
dnl binary compatibility, it is necessary that all gstreamer
dnl code be compiled with the same sizeof(off_t), so we use
dnl the following crude hack.
dnl

dnl
dnl GST_CFLAGS are split up as GST_EXT_CFLAGS and GST_INT_CFLAGS
dnl same for libs
dnl this is so we can make GST_CFLAGS for external modules available
dnl without mixing in internal (uninstalled) CFLAGS
dnl

AC_MSG_CHECKING(for large file support)
AC_TRY_RUN([
#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64
#include <sys/types.h>
int main () { return !(sizeof(off_t) == 8); }
],
[
AC_MSG_RESULT(yes)
GST_EXT_CFLAGS="-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64"
],
[
AC_MSG_RESULT(no)
],
[
AC_MSG_RESULT(no)
])

dnl Check for essential libraries first:
dnl ====================================

dnl === GLib 2 ===
dnl Minimum required version of GLib2
GLIB2_REQ="1.3.12"
AC_SUBST(GLIB2_REQ)

dnl Check for glib2
PKG_CHECK_MODULES(GLIB2, glib-2.0 >= $GLIB2_REQ gobject-2.0 gthread-2.0 gmodule-2.0,
  HAVE_GLIB2=yes,HAVE_GLIB2=no)
GLIB_LIBS=$GLIB2_LIBS
GLIB_CFLAGS=$GLIB2_CFLAGS
AC_SUBST(GLIB_LIBS)
AC_SUBST(GLIB_CFLAGS)

if test "x$HAVE_GLIB2" = "xno"; then
  AC_MSG_ERROR([GStreamer requires GLib 2.0 to compile.])
fi

GST_PKG_DEPS="glib-2.0, gobject-2.0, gmodule-2.0, gthread-2.0"
AC_SUBST(GST_PKG_DEPS)
  
dnl === libxml 2 ===
dnl Minimum required version of libxml2
LIBXML2_REQ="2.4.0"
AC_SUBST(LIBXML2_REQ)

dnl check for libxml2
LIBXML_PKG=', libxml-2.0'
PKG_CHECK_MODULES(XML, libxml-2.0 >= $LIBXML2_REQ, HAVE_LIBXML2=yes, HAVE_LIBXML2=no)
if test "x$HAVE_LIBXML2" = "xyes"; then
  AC_DEFINE(HAVE_LIBXML2)
else
  AC_MSG_ERROR([Need libxml2 for glib2 builds -- you should be able to do without it -- this needs fixing])
fi
AC_SUBST(LIBXML_PKG)
AC_SUBST(XML_LIBS)
AC_SUBST(XML_CFLAGS)

GST_CHECK_LIBHEADER(POPT, popt, poptStrippedArgv,, popt.h, POPT_LIBS="-lpopt",
  AC_MSG_ERROR([popt 1.5 or newer is required to build gstreamer. You can
download the latest version from ftp://people.redhat.com/sopwith/popt/]))

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

dnl ==========================================================================
dnl ============================= gst plugins ================================
dnl ==========================================================================
GST_PLUGIN_LDFLAGS='-module -avoid-version'
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl ######################################################################
dnl # Check command line parameters, and set shell variables accordingly #
dnl ######################################################################

dnl FIXME: simplify all this down using a few m4 macros

AC_ARG_ENABLE(libmmx,
AC_HELP_STRING([--enable-libmmx][use libmmx, if available]),
[case "${enableval}" in
  yes) USE_LIBMMX=$HAVE_LIBMMX ;;
  no)  USE_LIBMMX=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-libmmx) ;;
esac], 
[USE_LIBMMX=$HAVE_LIBMMX]) dnl Default value

AC_ARG_ENABLE(atomic,
AC_HELP_STRING([--enable-atomic][use atomic reference counting header]),
[case "${enableval}" in
  yes) USE_ATOMIC_H=$HAVE_ATOMIC_H;;
  noset) USE_ATOMIC_H=$HAVE_ATOMIC_H;;
  no)  USE_ATOMIC_H=no;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-atomic) ;;
esac], 
[USE_ATOMIC_H=$HAVE_ATOMIC_H]) dnl Default value

AC_ARG_ENABLE(plugin-builddir,
AC_HELP_STRING([--enable-plugin-builddir][allow tests/demos to use non-installed plugins]),
[case "${enableval}" in
  yes) PLUGINS_USE_BUILDDIR=yes ;;
  no)  PLUGINS_USE_BUILDDIR=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-plugin-builddir) ;;
esac], 
[PLUGINS_USE_BUILDDIR=no]) dnl Default value

GST_DEBUGINFO

AC_ARG_ENABLE(profiling,
AC_HELP_STRING([--enable-profiling][adds -pg to compiler commandline, for profiling]),
[case "${enableval}" in
  yes) USE_PROFILING=yes ;;
  no)  UES_PROFILING=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-profiling) ;;
esac], 
[USE_PROFILING=no]) dnl Default value

dnl default to building registry in the source tree if we are enabling plugin build dir
if test "x$PLUGINS_USE_BUILDDIR" = "xyes"; then
  GST_CONFIG_DIR=`pwd`
else
  GST_CONFIG_DIR=/etc/gstreamer
fi
AC_ARG_WITH(configdir,
AC_HELP_STRING([--with-configdir][specify path to use for plugin and command completion registries]),
[case "${withval}" in
  yes) AC_MSG_ERROR(bad value ${withval} for --with-configdir) ;;
  no) AC_MSG_ERROR(bad value ${withval} for --with-configdir) ;;
  *) GST_CONFIG_DIR="${withval}" ;;
esac], 
[:]) dnl Default value

AC_ARG_ENABLE(tests,
AC_HELP_STRING([--disable-tests][disable building test apps]),
[case "${enableval}" in
  yes) BUILD_TESTS=yes ;;
  no)  BUILD_TESTS=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-tests) ;;
esac], 
[BUILD_TESTS=yes]) dnl Default value

AC_ARG_ENABLE(examples,
AC_HELP_STRING([--disable-examples][disable building examples]),
[case "${enableval}" in
  yes) BUILD_EXAMPLES=yes ;;
  no)  BUILD_EXAMPLES=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-examples) ;;
esac], 
[BUILD_EXAMPLES=yes]) dnl Default value

dnl Next, check for the optional components:
dnl ========================================

translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_LOADSAVE, true)
GST_SUBSYSTEM_DISABLE(LOADSAVE,[pipeline XML load/save])
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_TYPEFIND, true)
GST_SUBSYSTEM_DISABLE(TYPEFIND,[typefind plugin],)
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_AUTOPLUG, true)
GST_SUBSYSTEM_DISABLE(AUTOPLUG,[autoplugger subsystem])
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_PARSE, true)
GST_SUBSYSTEM_DISABLE(PARSE,[command-line parser])
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_TRACE, true)
GST_SUBSYSTEM_DISABLE(TRACE,[tracing subsystem])
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_REGISTRY, true)
GST_SUBSYSTEM_DISABLE(REGISTRY,[plugin registry])

GST_EXT_CFLAGS="$GST_EXT_CFLAGS $GST_SUBSYSTEM_DISABLE_DEFINES"

dnl ################################################
dnl # Set defines according to variables set above #
dnl ################################################


dnl These should be "USE_*" instead of "HAVE_*", but some packages expect
dnl HAVE_ and it is likely to be easier to stick with the old name
if test "x$USE_LIBMMX" = xyes; then
  AC_DEFINE(HAVE_LIBMMX, 1, [Define if libmmx is available])
fi

if test "x$USE_ATOMIC_H" = xyes; then
  AC_DEFINE(HAVE_ATOMIC_H, 1, [Define if atomic.h header file is available])
fi

if test "x$PLUGINS_USE_BUILDDIR" = xyes; then
  AC_DEFINE(PLUGINS_USE_BUILDDIR, 1, [Define if plugins should be loaded from the build tree - only developers should use this])
fi

dnl if test "x$USE_DEBUG" = xyes; then
dnl   CFLAGS="$CFLAGS -g"
dnl fi

if test "x$USE_PROFILING" = xyes; then
dnl  CFLAGS="$CFLAGS -pg -fprofile-arcs"
  FOMIT_FRAME_POINTER=""
else
  FOMIT_FRAME_POINTER="-fomit-frame-pointer"
fi

dnl
dnl AC_SUBST(FOMIT_FRAME_POINTER)
dnl

dnl #############################
dnl # Set automake conditionals #
dnl #############################

dnl These should be "USE_*" instead of "HAVE_*", but some packages expect
dnl HAVE_ and it is likely to be easier to stick with the old name
AM_CONDITIONAL(HAVE_ATOMIC_H,       test "x$USE_ATOMIC_H" = "xyes")

AM_CONDITIONAL(EXPERIMENTAL,        test "$EXPERIMENTAL" = "$xyes")
AM_CONDITIONAL(BROKEN,              test "$BROKEN" = "$xyes")

AM_CONDITIONAL(HAVE_NASM,           test "x$HAVE_NASM" = "xyes")
AM_CONDITIONAL(BUILD_TESTS,         test "x$BUILD_TESTS" = "xyes")
AM_CONDITIONAL(BUILD_EXAMPLES,      test "x$BUILD_EXAMPLES" = "xyes")
AM_CONDITIONAL(PLUGINS_USE_BUILDDIR,  test "x$PLUGINS_USE_BUILDDIR" = "xyes")


dnl ############################
dnl # Set up some more defines #
dnl ############################

dnl Set location of configuration dir.
AC_DEFINE_UNQUOTED(GST_CONFIG_DIR,"$GST_CONFIG_DIR")
AC_SUBST(GST_CONFIG_DIR)

dnl Set location of plugin directory
if test "x${prefix}" = "xNONE"; then
  PLUGINS_DIR=${ac_default_prefix}/lib/gst
else
  PLUGINS_DIR=${prefix}/lib/gst
fi
AC_DEFINE_UNQUOTED(PLUGINS_DIR,"$PLUGINS_DIR")
AC_SUBST(PLUGINS_DIR)

dnl Set location of uninstalled plugin directory
PLUGINS_BUILDDIR=`pwd`
AC_DEFINE_UNQUOTED(PLUGINS_BUILDDIR,"$PLUGINS_BUILDDIR")
AC_SUBST(PLUGINS_BUILDDIR)

dnl since glib and xml are package deps, there's no need to include their cflags
dnl in the pkg-config file

dnl for pkg-config
GST_PKG_CFLAGS=$GST_EXT_CFLAGS
GST_PKG_LIBS=$GST_EXT_LIBS
AC_SUBST(GST_PKG_CFLAGS)
AC_SUBST(GST_PKG_LIBS)

dnl finalize _CFLAGS and _LIBS
dnl add GLIB and XML if necessary to EXT_*
GST_CFLAGS="$GST_EXT_CFLAGS $XML_CFLAGS $GLIB_CFLAGS"
GST_LIBS="$GST_EXT_LIBS $XML_LIBS $GLIB_LIBS -lpopt"

dnl Private vars for libgst only
LIBGST_LIBS="$GST_LIBS"
LIBGST_CFLAGS="$GST_CFLAGS -I\$(top_srcdir) -Wall"
AC_SUBST(LIBGST_LIBS)
AC_SUBST(LIBGST_CFLAGS)

dnl Vars for everyone else
GST_INT_LIBS="\$(top_builddir)/gst/libgstreamer.la"
GST_INT_CFLAGS="-I\$(top_srcdir)/libs -I\$(top_srcdir)/include"

AC_SUBST(GST_CFLAGS, "$LIBGST_CFLAGS $GST_INT_CFLAGS")
AC_SUBST(GST_LIBS, "$LIBGST_LIBS $GST_INT_LIBS")

AC_CONFIG_SUBDIRS(libs/ext/cothreads)

dnl ##################################################
dnl # Prepare informative messages to display at end #
dnl ##################################################

infomessages=

if test "x$PLUGINS_USE_BUILDDIR" = xyes; then
  infomessages="$infomessages
*** Warning: You have configured using the --enable-plugin-builddir option.

This option is for development purposes only: binaries built with
it should be used with code in the build tree only.  To build an
installable version, use ./configure without the --enable-plugin-builddir
option.  Note that the autogen.sh script supplies the plugin builddir
option automatically -- run ./autogen.sh --disable-plugin-buildddir to make
an installable build.

"
fi

dnl #########################
dnl # Make the output files #
dnl #########################

dnl libs/ext/Makefile
AC_OUTPUT(
Makefile
include/Makefile
gst/Makefile
gst/gstversion.h
gst/types/Makefile
gst/elements/Makefile
gst/autoplug/Makefile
gst/schedulers/Makefile
libs/Makefile
libs/gst/Makefile
libs/gst/bytestream/Makefile
libs/gst/getbits/Makefile
libs/gst/putbits/Makefile
libs/gst/control/Makefile
libs/ext/Makefile
tests/Makefile
tests/bufspeed/Makefile
tests/memchunk/Makefile
tests/muxing/Makefile
tests/sched/Makefile
testsuite/Makefile
testsuite/bytestream/Makefile
testsuite/caps/Makefile
testsuite/cleanup/Makefile
testsuite/elements/Makefile
testsuite/plugin/Makefile
testsuite/dynparams/Makefile
examples/Makefile
examples/autoplug/Makefile
examples/helloworld/Makefile
examples/helloworld2/Makefile
examples/launch/Makefile
examples/queue/Makefile
examples/queue2/Makefile
examples/queue3/Makefile
examples/queue4/Makefile
examples/thread/Makefile
examples/mixer/Makefile
examples/cutter/Makefile
examples/launch/Makefile
examples/xml/Makefile
examples/plugins/Makefile
examples/typefind/Makefile
examples/mixer/Makefile
tools/Makefile
docs/Makefile
docs/gst/Makefile
docs/gst/gstreamer.types
docs/libs/Makefile
docs/plugins/Makefile
docs/plugins/gstreamer-plugins.types
docs/manual/Makefile
docs/fwg/Makefile
docs/xsl/Makefile
docs/devhelp/Makefile
stamp.h
gstreamer.pc
gstreamer-uninstalled.pc
gstreamer.spec,
echo "$infomessages", infomessages="$infomessages"
)
