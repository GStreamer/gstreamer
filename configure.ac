AC_INIT
AC_CANONICAL_TARGET([])

dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AS_VERSION(gstreamer, GST_VERSION, 0, 8, 1, 2, GST_CVS="no", GST_CVS="yes")

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE
AM_INIT_AUTOMAKE($PACKAGE,$VERSION)

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$GST_VERSION_MAJOR.$GST_VERSION_MINOR
dnl we override it here for release candidates
#GST_MAJORMINOR=0.8
AC_SUBST(GST_MAJORMINOR)

AC_PROG_CC
dnl For interactive UNIX (a Sun thing)
AC_ISC_POSIX

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
AS_LIBTOOL(GST, 2, 0, 1)
AM_PROG_LIBTOOL

AC_CONFIG_SRCDIR([gst/gst.c])
AM_CONFIG_HEADER(config.h)

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I common/m4")

AM_PROG_CC_STDC
AM_PROG_AS
AS="${CC}"

dnl the gettext stuff needed
AM_GNU_GETTEXT_VERSION(0.11.5)
AM_GNU_GETTEXT([external])

GETTEXT_PACKAGE=gstreamer-$GST_MAJORMINOR
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], "$GETTEXT_PACKAGE",
                   [gettext package name])

dnl define LOCALEDIR in config.h
AS_AC_EXPAND(LOCALEDIR, $datadir/locale)
AC_DEFINE_UNQUOTED([LOCALEDIR], "$LOCALEDIR",
                   [gettext locale dir])


dnl decide on error flags
dnl if we support -Wall, set it unconditionally
AS_COMPILER_FLAG(-Wall,
                 GST_ERROR_CFLAGS="-Wall",
                 GST_ERROR_CFLAGS="")
dnl if we're in nano >= 1, add -Werror if supported
if test "x$GST_CVS" = "xyes"
then
  AS_COMPILER_FLAG(-Werror, GST_ERROR_CFLAGS="$GST_ERROR_CFLAGS -Werror")
fi

dnl We disable static building for development, for time savings
dnl *NOTE*: dnl this line before release, so release does static too
dnl AM_DISABLE_STATIC

AC_HEADER_STDC([])
AC_C_INLINE

dnl define correct errorlevel for debugging messages. We want to have GST_ERROR
dnl messages printed when running cvs builds
if test "x$GST_CVS" = "xyes"; then
  AC_DEFINE(GST_LEVEL_DEFAULT, GST_LEVEL_ERROR, [Default errorlevel to use])
fi

dnl subsystems - can influence other decisions so needs to be high up
dnl we need to do AM_CONDITIONAL them here for automake 1.6.x compatibility
GST_CHECK_SUBSYSTEM_DISABLE(GST_DEBUG,[debugging subsystem])
AM_CONDITIONAL(GST_DISABLE_GST_DEBUG, test "x$GST_DISABLE_GST_DEBUG" = "xyes")
GST_CHECK_SUBSYSTEM_DISABLE(LOADSAVE,[pipeline XML load/save])
AM_CONDITIONAL(GST_DISABLE_LOADSAVE, test "x$GST_DISABLE_LOADSAVE" = "xyes")
GST_CHECK_SUBSYSTEM_DISABLE(PARSE,[command-line parser])
AM_CONDITIONAL(GST_DISABLE_PARSE, test "x$GST_DISABLE_PARSE" = "xyes")
GST_CHECK_SUBSYSTEM_DISABLE(TRACE,[tracing subsystem])
AM_CONDITIONAL(GST_DISABLE_TRACE, test "x$GST_DISABLE_TRACE" = "xyes")
GST_CHECK_SUBSYSTEM_DISABLE(ALLOC_TRACE,[allocation tracing])
AM_CONDITIONAL(GST_DISABLE_ALLOC_TRACE, test "x$GST_DISABLE_ALLOC_TRACE" = "xyes")
GST_CHECK_SUBSYSTEM_DISABLE(REGISTRY,[plugin registry])
AM_CONDITIONAL(GST_DISABLE_REGISTRY, test "x$GST_DISABLE_REGISTRY" = "xyes")
GST_CHECK_SUBSYSTEM_DISABLE(ENUMTYPES,[enum types])
AM_CONDITIONAL(GST_DISABLE_ENUMTYPES, test "x$GST_DISABLE_ENUMTYPES" = "xyes")
GST_CHECK_SUBSYSTEM_DISABLE(INDEX,[index])
AM_CONDITIONAL(GST_DISABLE_INDEX, test "x$GST_DISABLE_INDEX" = "xyes")
GST_CHECK_SUBSYSTEM_DISABLE(PLUGIN,[plugin])
AM_CONDITIONAL(GST_DISABLE_PLUGIN, test "x$GST_DISABLE_PLUGIN" = "xyes")
GST_CHECK_SUBSYSTEM_DISABLE(URI,[uri handlers])
AM_CONDITIONAL(GST_DISABLE_URI, test "x$GST_DISABLE_URI" = "xyes")

dnl ##############################
dnl # Do automated configuration #
dnl ##############################

dnl Check for tools:
dnl ================

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE()

dnl modify pkg-config path
AC_ARG_WITH(pkg-config-path, 
   AC_HELP_STRING([--with-pkg-config-path],
                  [colon-separated list of pkg-config(1) dirs]),
   [export PKG_CONFIG_PATH=${withval}])

GST_DOC()
GST_ARCH()

dnl Perl is used in building documentation
AC_PATH_PROG(PERL_PATH, perl, no)
if test x$PERL_PATH = xno; then
  AC_MSG_ERROR(Could not find perl)
fi

AC_PROG_AWK

dnl we require bison for building the parser
dnl FIXME: check if AC_PROG_YACC is suitable here
AC_PATH_PROG(BISON_PATH, bison, no)
if test x$BISON_PATH = xno; then
  AC_MSG_ERROR(Could not find bison)
fi

dnl check bison version
AC_MSG_CHECKING([bison version])

if $BISON_PATH --version | head -n 1 | $AWK '{ if ($4 < 1.35) exit 1; else exit 0;}'; 
then 
  AC_MSG_RESULT([ok])
else 
  AC_MSG_RESULT([too old.])
  AC_MSG_ERROR([Your bison version is too old, v1.35 or later is required.])
fi

dnl we require flex for building the parser
dnl FIXME: check if AC_PROG_LEX is suitable here
AC_PATH_PROG(FLEX_PATH, flex, no)
if test x$FLEX_PATH = xno; then
  AC_MSG_ERROR(Could not find flex)
fi

dnl
dnl We should really use AC_SYS_LARGEFILE, but the problem is
dnl many of the plugins don't include "config.h".  To assure
dnl binary compatibility, it is necessary that all gstreamer
dnl code be compiled with the same sizeof(off_t), so we use
dnl the following crude hack.
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
  GST_PKG_CFLAGS="-D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64"
],
[
  AC_MSG_RESULT(no)
])

dnl check for mmap
AC_FUNC_MMAP()
if test "$ac_cv_func_mmap_fixed_mapped" = "yes" ; then
  GST_DISABLE_OMEGA_COTHREADS=no
else
  GST_DISABLE_OMEGA_COTHREADS=yes
fi
dnl for future --disabling, maybe.  Right now, it depends on HAVE_MMAP
AM_CONDITIONAL(GST_DISABLE_OMEGA_COTHREADS, test "x$GST_DISABLE_OMEGA_COTHREADS" = "xyes")

dnl check for makecontext and define HAVE_MAKECONTEXT if we have it
AC_CHECK_MCSC()
if test "$ac_cv_check_mcsc" = "yes"; then
  AC_DEFINE_UNQUOTED(HAVE_MAKECONTEXT, $HAVE_MAKECONTEXT,
                     [defined if we have makecontext ()])
fi

dnl Check for a way to display the function name in debug output
GST_CHECK_FUNCTION()

dnl check if we should use -Wa,-mregnames on PowerPC, so we can use
dnl symbolic register names in inline asm
if test x$HAVE_CPU_PPC = xyes ; then
  AS_COMPILER_FLAG(["-Wa,-mregnames"],
    [GST_INT_CFLAGS="$GST_INT_CFLAGS -Wa,-mregnames"],
    true)
fi

AC_ARG_ENABLE(gcov,
  AC_HELP_STRING([--enable-gcov],[compile with coverage profiling instrumentation (gcc only)]),
  enable_gcov=$enableval,enable_gcov=no)
if test x$enable_gcov = xyes ; then
  AS_COMPILER_FLAG(["-fprofile-arcs"],
    [GST_INT_CFLAGS="$GST_INT_CFLAGS -fprofile-arcs"],
    true)
  AS_COMPILER_FLAG(["-ftest-coverage"],
    [GST_INT_CFLAGS="$GST_INT_CFLAGS -ftest-coverage"],
    true)
  GST_INT_CFLAGS=`echo "$GST_INT_CFLAGS" | sed -e 's/-O[0-9]*//g'`

  AC_DEFINE_UNQUOTED(GST_GCOV_ENABLED, 1, [Defined if gcov is enabled to force a rebuild due to config.h changing])
fi
AM_CONDITIONAL(GST_GCOV_ENABLED, test x$enable_gcov = xyes)

dnl Check for some compiler flags that optimize our code.
dnl Only enable this for CVS builds for now
if test "x$GST_CVS" = "xyes"; then
  AS_COMPILER_FLAG(-fno-common,GST_INT_CFLAGS="$GST_INT_CFLAGS -fno-common",)
fi

dnl HAVE_WIN32 currently means "disable POSIXisms".
case "$host" in
  *-*-mingw*)
    AC_DEFINE_UNQUOTED(HAVE_WIN32, 1, [Defined if compiling for Windows])
    ;;
  *)
    ;;
esac

GST_UNALIGNED_ACCESS()
if test x${as_cv_unaligned_access} = xyes ; then
  GST_HAVE_UNALIGNED_ACCESS_DEFINE="#define GST_HAVE_UNALIGNED_ACCESS 1"
else
  GST_HAVE_UNALIGNED_ACCESS_DEFINE="#define GST_HAVE_UNALIGNED_ACCESS 0"
fi
AC_SUBST(GST_HAVE_UNALIGNED_ACCESS_DEFINE)

dnl Check for essential libraries first:
dnl ====================================

dnl === GLib 2 ===
dnl Minimum required version of GLib2
dnl required for compilation without warnings
GLIB2_REQ="2.2"
AC_SUBST(GLIB2_REQ)

dnl Check for glib2 with sugar on top
PKG_CHECK_MODULES(GLIB2, glib-2.0 >= $GLIB2_REQ gobject-2.0 gthread-2.0 gmodule-2.0,HAVE_GLIB2=yes,HAVE_GLIB2=no)
dnl for the poor souls who for example have glib in /usr/local
AS_SCRUB_INCLUDE(GLIB2_CFLAGS)
GLIB_LIBS=$GLIB2_LIBS
GLIB_CFLAGS=$GLIB2_CFLAGS
AC_SUBST(GLIB_LIBS)
AC_SUBST(GLIB_CFLAGS)

if test "x$HAVE_GLIB2" = "xno"; then
  AC_MSG_ERROR([GStreamer requires GLib $GLIB2_REQ to compile.])
fi

dnl Check for glib2 without extra fat, useful for the unversioned tool frontends
PKG_CHECK_MODULES(GLIB_ONLY, glib-2.0 >= $GLIB2_REQ)
AC_SUBST(GLIB_ONLY_CFLAGS)
AC_SUBST(GLIB_ONLY_LIBS)
  
dnl === libxml 2 ===
if test "x$GST_DISABLE_LOADSAVE" = "xyes" && test "x$GST_DISABLE_REGISTRY" = "xyes"
then
  AC_MSG_NOTICE([Registry and load/save are disabled, not checking for libxml2])
else
  dnl check for libxml2 with minimum req version
  GST_LIBXML2_CHECK(2.4.9)
fi

dnl popt checks
dnl FIXME: This test passes on popt 1.6.2, maybe earlier, but popt 1.6.2
dnl        causes segfaults on gst_init. Write a working test if possible.
GST_CHECK_LIBHEADER(POPT, popt, poptStrippedArgv,, popt.h, POPT_LIBS="-lpopt",
  AC_MSG_ERROR([popt 1.6.3 or newer is required to build gstreamer. You can
                download the latest version from 
                ftp://ftp.rpm.org/pub/rpm/dist/])
)
AC_MSG_NOTICE(Checking for POPT_TABLEEND)
AC_COMPILE_IFELSE([
#include <popt.h>
int main ()
{
#ifndef POPT_TABLEEND
#error
#else
  return 0;
#endif
}
],, [
  dnl it failed
  AC_MSG_ERROR([popt 1.6.3 or newer is required to build gstreamer. You can
                download the latest version from 
                ftp://ftp.rpm.org/pub/rpm/dist/])
])
AC_SUBST(POPT_LIBS)

dnl Check for ucontext.h
AC_CHECK_HEADER(ucontext.h, AC_DEFINE(HAVE_UCONTEXT_H, 1, [defined if we have ucontext.h]))

dnl ######################################################################
dnl # Check command line parameters, and set shell variables accordingly #
dnl ######################################################################

dnl FIXME: simplify all this down using a few m4 macros

AC_ARG_ENABLE(libmmx,
AC_HELP_STRING([--enable-libmmx],[use libmmx, if available]),
[case "${enableval}" in
  yes) USE_LIBMMX=$HAVE_LIBMMX ;;
  no)  USE_LIBMMX=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-libmmx) ;;
esac], 
[USE_LIBMMX=$HAVE_LIBMMX]) dnl Default value

AC_ARG_ENABLE(fast-stack-trash,
AC_HELP_STRING([--enable-fast-stack-trash],[use fast memory allocator (i586 or above)]),
[case "${enableval}" in
  yes) USE_FAST_STACK_TRASH=yes;;
  noset) USE_FAST_STACK_TRASH=no;;
  no)  USE_FAST_STACK_TRASH=no;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-atomic) ;;
esac], 
[USE_FAST_STACK_TRASH=yes]) dnl Default value

AC_ARG_ENABLE(plugin-builddir,
AC_HELP_STRING([--enable-plugin-builddir],[allow tests/demos to use non-installed plugins]),
[case "${enableval}" in
  yes) PLUGINS_USE_BUILDDIR=yes ;;
  no)  PLUGINS_USE_BUILDDIR=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-plugin-builddir) ;;
esac], 
[PLUGINS_USE_BUILDDIR=no]) dnl Default value

AC_ARG_ENABLE(profiling,
AC_HELP_STRING([--enable-profiling],[adds -pg to compiler commandline, for profiling]),
[case "${enableval}" in
  yes) USE_PROFILING=yes ;;
  no)  USE_PROFILING=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-profiling) ;;
esac], 
[USE_PROFILING=no]) dnl Default value

dnl use a cache dir for storing element registry info.
dnl default to building registry in the source tree if we are enabling plugin build dir
if test "x$PLUGINS_USE_BUILDDIR" = "xyes"; then
  GST_CACHE_DIR=`pwd`
else
  dnl ${localstatedir} points to PREFIX/var
  GST_CACHE_DIR=${localstatedir}/cache/gstreamer-$GST_MAJORMINOR
fi
AC_ARG_WITH(cachedir,
AC_HELP_STRING([--with-cachedir],[specify path to use for plugin and command completion registries]),
[case "${withval}" in
  yes) AC_MSG_ERROR(bad value ${withval} for --with-cachedir) ;;
  no) AC_MSG_ERROR(bad value ${withval} for --with-cachedir) ;;
  *) GST_CACHE_DIR="${withval}" ;;
esac], 
[:]) dnl Default value

AS_AC_EXPAND(GST_CACHE_DIR, $GST_CACHE_DIR)
AC_DEFINE_UNQUOTED(GST_CACHE_DIR, "$GST_CACHE_DIR", [Location of registry])
AC_MSG_NOTICE(Using $GST_CACHE_DIR as registry cache dir)

dnl building of tests
AC_ARG_ENABLE(tests,
AC_HELP_STRING([--disable-tests],[disable building test apps]),
[case "${enableval}" in
  yes) BUILD_TESTS=yes ;;
  no)  BUILD_TESTS=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-tests) ;;
esac], 
[BUILD_TESTS=yes]) dnl Default value
AM_CONDITIONAL(BUILD_TESTS,         test "x$BUILD_TESTS" = "xyes")

dnl tests known to fail
AC_ARG_ENABLE(failing-tests,
AC_HELP_STRING([--disable-failing-tests],[disable building tests known to fail]),
[case "${enableval}" in
  yes) BUILD_FAILING_TESTS=yes ;;
  no)  BUILD_FAILING_TESTS=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-failing-tests) ;;
esac], 
[BUILD_FAILING_TESTS=no]) dnl Default value
AM_CONDITIONAL(BUILD_FAILING_TESTS, test "x$BUILD_FAILING_TESTS" = "xyes")
if test x$BUILD_FAILING_TESTS = xyes; then
  AC_MSG_WARN([building tests known to fail, use --disable-failing-tests to disable])
else
  AC_MSG_WARN([Sissy ! By asking to not build the tests known to fail, you hereby waive your right to customer support.  If you do not agree with this EULA, please press Ctrl-C before the next line is printed.  By allowing the next line to be printed, you expressly acknowledge your acceptance of this EULA.])
fi

AC_ARG_ENABLE(examples,
AC_HELP_STRING([--disable-examples],[disable building examples]),
[case "${enableval}" in
  yes) BUILD_EXAMPLES=yes ;;
  no)  BUILD_EXAMPLES=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-examples) ;;
esac], 
[BUILD_EXAMPLES=yes]) dnl Default value
AM_CONDITIONAL(BUILD_EXAMPLES,      test "x$BUILD_EXAMPLES" = "xyes")

dnl poison destroyed objects
AC_ARG_ENABLE(poisoning,
AC_HELP_STRING([--enable-poisoning],[enable poisoning of deallocated objects]),
[case "${enableval}" in
  yes) USE_POISONING=yes ;;
  no)  USE_POISONING=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-poisoning) ;;
esac], 
[USE_POISONING=no]) dnl Default value

dnl Next, check for the optional components:
dnl ========================================

dnl debugging stuff
AC_ARG_ENABLE(debug,
AC_HELP_STRING([--disable-debug],[disable addition of -g debugging info]),
[case "${enableval}" in
  yes) USE_DEBUG=yes ;;
  no)  USE_DEBUG=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
esac],
[USE_DEBUG=yes]) dnl Default value

dnl valgrind inclusion
USE_VALGRIND="$USE_DEBUG"
if test "x$USE_VALGRIND" = xyes; then
  PKG_CHECK_MODULES(VALGRIND, valgrind, USE_VALGRIND="yes", USE_VALGRIND="no")
fi
if test "x$USE_VALGRIND" = xyes; then
  AC_DEFINE(HAVE_VALGRIND, 1, [Define if valgrind should be used])
  AC_MSG_NOTICE(Using extra code paths for valgrind)
fi
AC_SUBST(VALGRIND_CFLAGS)
AC_SUBST(VALGRIND_LIBS)

dnl ################################################
dnl # Set defines according to variables set above #
dnl ################################################


dnl These should be "USE_*" instead of "HAVE_*", but some packages expect
dnl HAVE_ and it is likely to be easier to stick with the old name
if test "x$USE_LIBMMX" = xyes; then
  AC_DEFINE(HAVE_LIBMMX, 1, [Define if libmmx is available])
fi

if test "x$USE_FAST_STACK_TRASH" = xyes; then
  AC_DEFINE(USE_FAST_STACK_TRASH, 1, [Define if we should use i586 optimized stack functions])
fi

if test "x$USE_POISONING" = xyes; then
  AC_DEFINE(USE_POISONING, 1, [Define if we should poison deallocated memory])
fi

dnl test for sigaction()
AC_CHECK_FUNC(sigaction,
   AC_DEFINE(HAVE_SIGACTION, 1,
             [Defined if we have sigaction ()]))

dnl test for register_printf_function
AC_CHECK_FUNC(register_printf_function,
  [
    GST_PRINTF_EXTENSION_FORMAT_DEFINE="#define GST_PTR_FORMAT \"P\""
    AC_DEFINE(HAVE_PRINTF_EXTENSION, 1,
             [Defined if we have register_printf_function ()])
  ],
  GST_PRINTF_EXTENSION_FORMAT_DEFINE="#define GST_PTR_FORMAT \"p\""
)
AC_SUBST(GST_PRINTF_EXTENSION_FORMAT_DEFINE)

dnl test if we have dladdr(); we use it for debugging
save_cflags="$CFLAGS"
CFLAGS="$CFLAGS -D_GNU_SOURCE"
AC_CHECK_LIB(dl, dladdr,
   AC_DEFINE(HAVE_DLADDR, 1,
             [Defined if we have dladdr ()]))
CFLAGS="$save_cflags"

if test "x$PLUGINS_USE_BUILDDIR" = xyes; then
  AC_DEFINE(PLUGINS_USE_BUILDDIR, 1, [Define if plugins should be loaded from the build tree - only developers should use this])
fi

if test "x$USE_DEBUG" = xyes; then
   GST_INT_CFLAGS="$GST_INT_CFLAGS -g"
fi

dnl #############################
dnl # Set automake conditionals #
dnl #############################

dnl These should be "USE_*" instead of "HAVE_*", but some packages expect
dnl HAVE_ and it is likely to be easier to stick with the old name
AM_CONDITIONAL(EXPERIMENTAL,        test "$EXPERIMENTAL" = "$xyes")
AM_CONDITIONAL(BROKEN,              test "$BROKEN" = "$xyes")
AM_CONDITIONAL(PLUGINS_USE_BUILDDIR,  test "x$PLUGINS_USE_BUILDDIR" = "xyes")


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
[GST_ORIGIN="http://gstreamer.net/"]) dnl Default value
AC_MSG_NOTICE(Using $GST_ORIGIN as package origin)
AC_DEFINE_UNQUOTED(GST_ORIGIN, "$GST_ORIGIN", [package origin])

dnl Set location of plugin directory
AS_AC_EXPAND(PLUGINS_DIR, ${libdir}/gstreamer-$GST_MAJORMINOR)
AC_DEFINE_UNQUOTED(PLUGINS_DIR, "$PLUGINS_DIR", [Define the plugin directory])
AC_SUBST(PLUGINS_DIR)
AC_MSG_NOTICE([Using $PLUGINS_DIR as the plugin install location])

dnl Set location of uninstalled plugin directory
PLUGINS_BUILDDIR=`pwd`
AC_DEFINE_UNQUOTED(PLUGINS_BUILDDIR, "$PLUGINS_BUILDDIR", [Define the uninstalled plugin directory])
AC_SUBST(PLUGINS_BUILDDIR)

dnl LDFLAGS modifier defining exported symbols from built objects
EXPORT_LDFLAGS="-export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*"

dnl GST_PKG_*:    the flags we use that are also used
dnl               for external applications/.pc file
dnl GST_LIB_*:    used for the main library
dnl GST_OBJ_*:    the flags we use for all the objects internal to the core
dnl GST_PLUGIN_*: additional flags we use for all plugins

dnl finalize _CFLAGS and _LIBS
dnl flags exported for external applications and use in our pkg-config .pc files
dnl since glib and xml are package deps, there's no need to include their cflags
dnl in the pkg-config file

GST_PKG_DEPS="glib-2.0, gobject-2.0, gmodule-2.0, gthread-2.0"
AC_SUBST(GST_PKG_CFLAGS)
AC_SUBST(GST_PKG_LIBS)
AC_SUBST(GST_PKG_DEPS)

dnl flags shared for all internal objects (core libs, elements, applications)
dnl we disable deprecated internally
dnl XML, GLib, popt, GST_INT, VALGRIND, and the right include for CFLAGS
dnl no need to add XML, GLib, popt explicitly since libgstreamer pulls them in
GST_INT_CFLAGS="$GLIB_CFLAGS $XML_CFLAGS $GST_PKG_CFLAGS \
 $GST_INT_CFLAGS $GST_ERROR_CFLAGS -DGST_DISABLE_DEPRECATED"

dnl Private vars for libgst only
GST_LIB_CFLAGS="$GST_PKG_CFLAGS $GST_INT_CFLAGS \
  $VALGRIND_CFLAGS -I\$(top_srcdir)"
GST_LIB_LIBS="$XML_LIBS $GLIB_LIBS -lpopt $GST_PKG_LIBS $VALGRIND_LIBS"
GST_LIB_LDFLAGS="$GST_LT_LDFLAGS -version-info $GST_LIBVERSION $EXPORT_LDFLAGS"
AC_SUBST(GST_LIB_CFLAGS)
AC_SUBST(GST_LIB_LIBS)
AC_SUBST(GST_LIB_LDFLAGS)

dnl Vars for all internal objects built on libgstreamer
GST_OBJ_CFLAGS="$GST_INT_CFLAGS -I\$(top_srcdir)/libs -I\$(top_srcdir)"
GST_OBJ_LIBS="\$(top_builddir)/gst/libgstreamer-$GST_MAJORMINOR.la"

AC_SUBST(GST_OBJ_CFLAGS, "$GST_OBJ_CFLAGS")
AC_SUBST(GST_OBJ_LIBS, "$GST_OBJ_LIBS")

dnl specific additional LDFLAGS for plugins
GST_PLUGIN_LDFLAGS="-module -avoid-version $EXPORT_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS, "$GST_PLUGIN_LDFLAGS")

plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"
AC_SUBST(plugindir)

dnl ##################################################
dnl # deps for examples from manual                  #
dnl ##################################################

PKG_CHECK_MODULES(LIBGNOMEUI, libgnomeui-2.0,
                  HAVE_LIBGNOMEUI="yes", HAVE_LIBGNOMEUI="no")
AC_SUBST(LIBGNOMEUI_CFLAGS)
AC_SUBST(LIBGNOMEUI_LIBS)
AM_CONDITIONAL(HAVE_LIBGNOMEUI, test "x$HAVE_LIBGNOMEUI" = "xyes")

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
option automatically -- run ./autogen.sh -- --disable-plugin-builddir to make
an installable build.

"
fi

dnl #########################
dnl # Make the output files #
dnl #########################

dnl libs/ext/Makefile
dnl nothing there yet !
AC_OUTPUT(
Makefile
include/Makefile
gst/Makefile
gst/gstconfig.h
gst/gstversion.h
gst/autoplug/Makefile
gst/indexers/Makefile
gst/elements/Makefile
gst/parse/Makefile
gst/schedulers/Makefile
gst/registries/Makefile
libs/Makefile
libs/gst/Makefile
libs/gst/bytestream/Makefile
libs/gst/control/Makefile
libs/gst/dataprotocol/Makefile
libs/gst/getbits/Makefile
po/Makefile.in
tests/Makefile
tests/bufspeed/Makefile
tests/instantiate/Makefile
tests/memchunk/Makefile
tests/muxing/Makefile
tests/seeking/Makefile
tests/sched/Makefile
tests/threadstate/Makefile
testsuite/Makefile
testsuite/bins/Makefile
testsuite/bytestream/Makefile
testsuite/caps/Makefile
testsuite/cleanup/Makefile
testsuite/clock/Makefile
testsuite/debug/Makefile
testsuite/dlopen/Makefile
testsuite/dynparams/Makefile
testsuite/elements/Makefile
testsuite/ghostpads/Makefile
testsuite/indexers/Makefile
testsuite/negotiation/Makefile
testsuite/parse/Makefile
testsuite/plugin/Makefile
testsuite/refcounting/Makefile
testsuite/schedulers/Makefile
testsuite/tags/Makefile
testsuite/threads/Makefile
examples/Makefile
examples/cutter/Makefile
examples/helloworld/Makefile
examples/launch/Makefile
examples/manual/Makefile
examples/mixer/Makefile
examples/pingpong/Makefile
examples/plugins/Makefile
examples/queue/Makefile
examples/queue2/Makefile
examples/queue3/Makefile
examples/queue4/Makefile
examples/retag/Makefile
examples/thread/Makefile
examples/typefind/Makefile
examples/xml/Makefile
tools/Makefile
common/Makefile
common/m4/Makefile
docs/Makefile
docs/faq/Makefile
docs/gst/Makefile
docs/libs/Makefile
docs/manual/Makefile
docs/plugins/Makefile
docs/plugins/gstreamer-plugins.types
docs/pwg/Makefile
docs/xsl/Makefile
docs/version.entities
pkgconfig/Makefile
stamp.h
pkgconfig/gstreamer.pc
pkgconfig/gstreamer-uninstalled.pc
pkgconfig/gstreamer-control.pc
pkgconfig/gstreamer-control-uninstalled.pc
gstreamer.spec,
echo "$infomessages", infomessages="$infomessages"
)

