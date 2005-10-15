AC_PREREQ(2.52)

dnl initialize autoconf
dnl we don't specify PACKAGE and VERSION because AS_VERSION puts it together
AC_INIT

dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AS_VERSION(gstreamer, GST_VERSION, 0, 9, 3, 1,
    GST_CVS="no", GST_CVS="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/gst.c])

dnl define the output header for config
AM_CONFIG_HEADER([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl initialize automake
dnl FIXME: this is deprecated use, we should move PACKAGE and VERSION to
dnl AC_INIT, but then we need to parse the version to pass to AS_VERSION
AM_INIT_AUTOMAKE($PACKAGE, $VERSION)

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$GST_VERSION_MAJOR.$GST_VERSION_MINOR
dnl we override it here for release candidates for a new series
# GST_MAJORMINOR=0.9
AC_SUBST(GST_MAJORMINOR)

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
AS_LIBTOOL(GST, 7, 0, 0)
AS_LIBTOOL_TAGS
AM_PROG_LIBTOOL

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I common/m4 -I .")

dnl *** check for arguments to configure ***

dnl subsystems - can influence other decisions so needs to be high up
dnl we need to AM_CONDITIONAL them here for automake 1.6.x compatibility
GST_CHECK_SUBSYSTEM_DISABLE(GST_DEBUG,[debugging subsystem])
AM_CONDITIONAL(GST_DISABLE_GST_DEBUG, test "x$GST_GST_DISABLE_DEBUG" = "xyes")
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

dnl FIXME: simplify all this down using a few m4 macros

dnl possibly modify pkg-config path
AC_ARG_WITH(pkg-config-path, 
   AC_HELP_STRING([--with-pkg-config-path],
                  [colon-separated list of pkg-config(1) dirs]),
   [export PKG_CONFIG_PATH=${withval}])

AC_ARG_ENABLE(fast-stack-trash,
  AC_HELP_STRING([--enable-fast-stack-trash],
    [use fast memory allocator (i586 or above)]),
  [
    case "${enableval}" in
      yes)   USE_FAST_STACK_TRASH=yes;;
      noset) USE_FAST_STACK_TRASH=no;;
      no)    USE_FAST_STACK_TRASH=no;;
      *)     AC_MSG_ERROR(bad value ${enableval} for --enable-atomic) ;;
    esac], 
  [USE_FAST_STACK_TRASH=yes]) dnl Default value
if test "x$USE_FAST_STACK_TRASH" = xyes; then
  AC_DEFINE(USE_FAST_STACK_TRASH, 1,
    [Define if we should use i586 optimized stack functions])
fi

AC_ARG_ENABLE(profiling,
  AC_HELP_STRING([--enable-profiling],
    [adds -pg to compiler commandline, for profiling]),
  [
    case "${enableval}" in
      yes) USE_PROFILING=yes ;;
      no)  USE_PROFILING=no ;;
      *)   AC_MSG_ERROR(bad value ${enableval} for --enable-profiling) ;;
    esac
  ], 
  [USE_PROFILING=no]) dnl Default value

dnl use a cache dir for storing element registry info.
GST_CACHE_DIR=${localstatedir}/cache/gstreamer-$GST_MAJORMINOR

AC_ARG_WITH(cachedir,
  AC_HELP_STRING([--with-cachedir],
    [specify path to use for plugin and command completion registries]),
  [
    case "${withval}" in
      yes) AC_MSG_ERROR(bad value ${withval} for --with-cachedir) ;;
      no)  AC_MSG_ERROR(bad value ${withval} for --with-cachedir) ;;
      *)   GST_CACHE_DIR="${withval}" ;;
    esac
  ], 
  [:]) dnl Default value

AS_AC_EXPAND(GST_CACHE_DIR, $GST_CACHE_DIR)
AC_DEFINE_UNQUOTED(GST_CACHE_DIR, "$GST_CACHE_DIR", [Location of registry])
AC_MSG_NOTICE(Using $GST_CACHE_DIR as registry cache dir)

dnl building of tests
AC_ARG_ENABLE(tests,
  AC_HELP_STRING([--disable-tests], [disable building test apps]),
  [
    case "${enableval}" in
      yes) BUILD_TESTS=yes ;;
      no)  BUILD_TESTS=no ;;
      *)   AC_MSG_ERROR(bad value ${enableval} for --disable-tests) ;;
    esac
  ], 
[BUILD_TESTS=yes]) dnl Default value
AM_CONDITIONAL(BUILD_TESTS,         test "x$BUILD_TESTS" = "xyes")

dnl tests known to fail
AC_ARG_ENABLE(failing-tests,
  AC_HELP_STRING([--disable-failing-tests],
    [disable building tests known to fail]),
  [
    case "${enableval}" in
      yes) BUILD_FAILING_TESTS=yes ;;
      no)  BUILD_FAILING_TESTS=no ;;
      *)   AC_MSG_ERROR(bad value ${enableval} for --disable-failing-tests) ;;
    esac
  ], 
  [BUILD_FAILING_TESTS=no]) dnl Default value
AM_CONDITIONAL(BUILD_FAILING_TESTS, test "x$BUILD_FAILING_TESTS" = "xyes")
if test x$BUILD_FAILING_TESTS = xyes; then
  AC_MSG_WARN([building tests known to fail, use --disable-failing-tests to disable])
else
  AC_MSG_WARN([Sissy ! By asking to not build the tests known to fail, you hereby waive your right to customer support.  If you do not agree with this EULA, please press Ctrl-C before the next line is printed.  By allowing the next line to be printed, you expressly acknowledge your acceptance of this EULA.])
fi

AC_ARG_ENABLE(examples,
  AC_HELP_STRING([--disable-examples], [disable building examples]),
    [
      case "${enableval}" in
        yes) BUILD_EXAMPLES=yes ;;
        no)  BUILD_EXAMPLES=no ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --disable-examples) ;;
      esac
    ], 
    [BUILD_EXAMPLES=yes]) dnl Default value
AM_CONDITIONAL(BUILD_EXAMPLES,      test "x$BUILD_EXAMPLES" = "xyes")

dnl poison destroyed objects
AC_ARG_ENABLE(poisoning,
  AC_HELP_STRING([--enable-poisoning],
    [enable poisoning of deallocated objects]),
  [
    case "${enableval}" in
      yes) USE_POISONING=yes ;;
      no)  USE_POISONING=no ;;
      *)   AC_MSG_ERROR(bad value ${enableval} for --enable-poisoning) ;;
    esac
  ], 
  [USE_POISONING=no]) dnl Default value
if test "x$USE_POISONING" = xyes; then
  AC_DEFINE(USE_POISONING, 1,
    [Define if we should poison deallocated memory])
fi

dnl debugging stuff
AC_ARG_ENABLE(debug,
  AC_HELP_STRING([--disable-debug],[disable addition of -g debugging info]),
  [
    case "${enableval}" in
      yes) USE_DEBUG=yes ;;
      no)  USE_DEBUG=no ;;
      *)   AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
    esac
  ],
  [USE_DEBUG=yes]) dnl Default value

dnl valgrind inclusion
AC_ARG_ENABLE(valgrind,
  AC_HELP_STRING([--disable-valgrind],[disable run-time valgrind detection]),
  [
    case "${enableval}" in
      yes) USE_VALGRIND="$USE_DEBUG" ;;
      no)  USE_VALGRIND=no ;;
      *)   AC_MSG_ERROR(bad value ${enableval} for --enable-valgrind) ;;
    esac
  ],
  [USE_VALGRIND="$USE_DEBUG"]) dnl Default value
VALGRIND_REQ="2.1"
if test "x$USE_VALGRIND" = xyes; then
  PKG_CHECK_MODULES(VALGRIND, valgrind > $VALGRIND_REQ,
    USE_VALGRIND="yes", USE_VALGRIND="no")
fi
if test "x$USE_VALGRIND" = xyes; then
  AC_DEFINE(HAVE_VALGRIND, 1, [Define if valgrind should be used])
  AC_MSG_NOTICE(Using extra code paths for valgrind)
fi

AC_ARG_ENABLE(gcov,
  AC_HELP_STRING([--enable-gcov],
    [compile with coverage profiling instrumentation (gcc only)]),
  enable_gcov=$enableval,
  enable_gcov=no)
if test x$enable_gcov = xyes ; then
  AS_COMPILER_FLAG(["-fprofile-arcs"],
    [GCOV_CFLAGS="$GCOV_CFLAGS -fprofile-arcs"],
    true)
  AS_COMPILER_FLAG(["-ftest-coverage"],
    [GCOV_CFLAGS="$GCOV_CFLAGS -ftest-coverage"],
    true)
  dnl remove any -O flags - FIXME: is this needed ?
  GCOV_CFLAGS=`echo "$GCOV_CFLAGS" | sed -e 's/-O[0-9]*//g'`

  AC_DEFINE_UNQUOTED(GST_GCOV_ENABLED, 1,
    [Defined if gcov is enabled to force a rebuild due to config.h changing])
fi
AM_CONDITIONAL(GST_GCOV_ENABLED, test x$enable_gcov = xyes)

dnl package name in plugins
AC_ARG_WITH(package-name,
  AC_HELP_STRING([--with-package-name],
    [specify package name to use in plugins]),
  [
    case "${withval}" in
      yes) AC_MSG_ERROR(bad value ${withval} for --with-package-name) ;;
      no)  AC_MSG_ERROR(bad value ${withval} for --with-package-name) ;;
      *)   GST_PACKAGE="${withval}" ;;
    esac
  ], 
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
  AC_HELP_STRING([--with-package-origin],
    [specify package origin URL to use in plugins]),
  [
    case "${withval}" in
      yes) AC_MSG_ERROR(bad value ${withval} for --with-package-origin) ;;
      no)  AC_MSG_ERROR(bad value ${withval} for --with-package-origin) ;;
      *)   GST_ORIGIN="${withval}" ;;
    esac
  ], 
  [GST_ORIGIN="http://gstreamer.freedesktop.org/"] dnl Default value
)
AC_MSG_NOTICE(Using $GST_ORIGIN as package origin)
AC_DEFINE_UNQUOTED(GST_ORIGIN, "$GST_ORIGIN", [package origin])

dnl *** platform checks ***

dnl * hardware/architecture *

dnl common/m4/gst-arch.m4
dnl check CPU type
GST_ARCH

dnl common/m4/gst-arch.m4
dnl check for unaligned access
GST_UNALIGNED_ACCESS
dnl create a configure variable for gst/gstconfig.h
if test x${as_cv_unaligned_access} = xyes ; then
  GST_HAVE_UNALIGNED_ACCESS_DEFINE="#define GST_HAVE_UNALIGNED_ACCESS 1"
else
  GST_HAVE_UNALIGNED_ACCESS_DEFINE="#define GST_HAVE_UNALIGNED_ACCESS 0"
fi
AC_SUBST(GST_HAVE_UNALIGNED_ACCESS_DEFINE)


dnl * software *

dnl check for large file support
dnl affected plugins must include config.h
AC_SYS_LARGEFILE

dnl HAVE_WIN32 currently means "disable POSIXisms".
case "$host" in
  *-*-mingw*)
    AC_DEFINE_UNQUOTED(HAVE_WIN32, 1, [Defined if compiling for Windows])
    ;;
  *)
    ;;
esac

dnl *** checks for programs ***

dnl find a compiler
AC_PROG_CC

dnl find an assembler
AM_PROG_AS

dnl we use awk in the bison version check
AC_PROG_AWK

dnl we require bison for building the parser
dnl FIXME: check if AC_PROG_YACC is suitable here
dnl FIXME: make precious
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

dnl Perl is used in building documentation
AC_PATH_PROG(PERL_PATH, perl, no)
if test x$PERL_PATH = xno; then
  AC_MSG_ERROR(Could not find perl)
fi

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for documentation tools
GST_DOCBOOK_CHECK
GTK_DOC_CHECK([1.3])

dnl *** checks for libraries ***

dnl For interactive UNIX (a Sun thing)
dnl FIXME: this adds -lcposix to LIBS, but I doubt we use LIBS
AC_ISC_POSIX

dnl *** checks for header files ***

dnl check if we have ANSI C header files
AC_HEADER_STDC

dnl Check for ucontext.h
AC_CHECK_HEADERS([ucontext.h])

dnl *** checks for library functions ***

AC_CHECK_FUNCS([sigaction])

dnl check for fseeko()
AC_FUNC_FSEEKO
dnl check for ftello()
AC_CHECK_FUNCS([ftello])

AC_CHECK_FUNCS([fgetpos])
AC_CHECK_FUNCS([fsetpos])

dnl check for mmap()
AC_FUNC_MMAP

dnl common/m4/gst-mcsc.m4
dnl check for makecontext and define HAVE_MAKECONTEXT if we have it
AC_CHECK_MCSC
if test "$ac_cv_check_mcsc" = "yes"; then
  AC_DEFINE_UNQUOTED(HAVE_MAKECONTEXT, $HAVE_MAKECONTEXT,
                     [defined if we have makecontext ()])
fi

dnl Check for a way to display the function name in debug output
GST_CHECK_FUNCTION

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

dnl test if we have dladdr(); we use it for debugging; see gst/gstinfo.c
save_cflags="$CFLAGS"
CFLAGS="$CFLAGS -D_GNU_SOURCE"
AC_CHECK_LIB(dl, dladdr,
   AC_DEFINE(HAVE_DLADDR, 1,
             [Defined if we have dladdr ()]))
CFLAGS="$save_cflags"

dnl *** checks for compiler characteristics ***

dnl make sure we can use "inline" from C code
AC_C_INLINE

dnl *** TODO: decide where to put ***

dnl set up gettext
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

dnl define LIBDIR so we can inform people where we live
AS_AC_EXPAND(LIBDIR, $libdir)
AC_DEFINE_UNQUOTED([LIBDIR], "$LIBDIR",
                   [library dir])

dnl define an ERROR_CFLAGS Makefile variable
GST_SET_ERROR_CFLAGS($GST_CVS)

dnl define correct errorlevel for debugging messages. We want to have GST_ERROR
dnl messages printed when running cvs builds
if test "x$GST_CVS" = "xyes"; then
  AC_DEFINE(GST_LEVEL_DEFAULT, GST_LEVEL_ERROR, [Default errorlevel to use])
fi

dnl *** checks for dependency libraries ***

dnl GLib

dnl Minimum required version of GLib
GLIB_REQ="2.6"
AC_SUBST(GLIB_REQ)

dnl Check for glib2 with sugar on top
PKG_CHECK_MODULES(GLIB,
    glib-2.0 >= $GLIB_REQ gobject-2.0 gthread-2.0 gmodule-2.0,
    HAVE_GLIB=yes,
    HAVE_GLIB=no)

if test "x$HAVE_GLIB" = "xno"; then
  AC_MSG_ERROR([GStreamer requires GLib >= $GLIB_REQ to compile.])
fi

dnl for the poor souls who for example have glib in /usr/local
AS_SCRUB_INCLUDE(GLIB_CFLAGS)

dnl GLib 2.8 has some bug fixes that we like, so make sure our code knows
AC_MSG_CHECKING([glib version >= 2.8])
if pkg-config --atleast-version=2.8 glib-2.0; then
  AC_MSG_RESULT(yes)
  GST_HAVE_GLIB_2_8_DEFINE="#define GST_HAVE_GLIB_2_8 1"
else
  AC_MSG_RESULT(no)
  GST_HAVE_GLIB_2_8_DEFINE="/* #undef GST_HAVE_GLIB_2_8 */"
fi
AC_SUBST(GST_HAVE_GLIB_2_8_DEFINE)

dnl Check for glib2 without extra fat, useful for the unversioned tool frontends
PKG_CHECK_MODULES(GLIB_ONLY, glib-2.0 >= $GLIB_REQ)
  
dnl libxml 2, which is/should be optional (FIXME)
if test "x$GST_DISABLE_LOADSAVE" = "xyes" && \
   test "x$GST_DISABLE_REGISTRY" = "xyes"
then
  AC_MSG_NOTICE([Registry and load/save are disabled, not checking for libxml2])
else
  dnl check for libxml2 with minimum req version
  GST_LIBXML2_CHECK(2.4.9)
fi

dnl check for "check", unit testing library/header
AM_PATH_CHECK(0.9.2,
    [
        HAVE_CHECK=yes
        AC_MSG_NOTICE(CHECK_CFLAGS: $CHECK_CFLAGS)
        AC_MSG_NOTICE(CHECK_LIBS: $CHECK_LIBS)
    ],
    HAVE_CHECK=no)
AM_CONDITIONAL(HAVE_CHECK, test "x$HAVE_CHECK" = "xyes")

OPT_CFLAGS=
dnl Check for some compiler flags that optimize our code.
if test "x$GCC" = xyes; then
  AS_COMPILER_FLAG(-fno-common, OPT_CFLAGS="$OPT_CFLAGS -fno-common")
fi

dnl FIXME: decide what kind of flag this is, maybe it's not an optimization
dnl flag
dnl check if we should use -Wa,-mregnames on PowerPC, so we can use
dnl symbolic register names in inline asm
if test x$HAVE_CPU_PPC = xyes ; then
  AS_COMPILER_FLAG(["-Wa,-mregnames"],
    [OPT_CFLAGS="$OPT_CFLAGS -Wa,-mregnames"])
fi

dnl *** FIXME: name this part

dnl set license and copyright notice
AC_DEFINE(GST_LICENSE, "LGPL", [GStreamer license])

dnl Set location of plugin directory
AS_AC_EXPAND(PLUGINS_DIR, ${libdir}/gstreamer-$GST_MAJORMINOR)
AC_DEFINE_UNQUOTED(PLUGINS_DIR, "$PLUGINS_DIR", [Define the plugin directory])
AC_SUBST(PLUGINS_DIR)
AC_MSG_NOTICE([Using $PLUGINS_DIR as the plugin install location])

dnl plugin directory configure-time variable for use in Makefile.am
plugindir="\$(libdir)/gstreamer-$GST_MAJORMINOR"
AC_SUBST(plugindir)

dnl FIXME: add LIBXML_PKG here
GST_PKG_DEPS="glib-2.0, gobject-2.0, gmodule-2.0, gthread-2.0"
AC_SUBST(GST_PKG_DEPS)

dnl *** finalize CFLAGS, LDFLAGS, LIBS

dnl Overview:
dnl GST_OPTION_CFLAGS:  common flags for profiling, debugging, errors, ...
dnl GST_ALL_*:          flags shared by all built objects
dnl GST_OBJ_*:          additional flags to link to the core library
dnl			include GST_ALL_*
dnl GST_LT_LDFLAGS:	library versioning of our libraries
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

dnl GST_ALL_*
dnl vars common to for all internal objects (core libs, elements, applications)
dnl CFLAGS: XML needs to be added because xmlPtr is used in core
dnl LIBS: XML doesn't need to be added because we don't explicitly use symbols
dnl       from LibXML
GST_ALL_CFLAGS="$GLIB_CFLAGS $XML_CFLAGS \$(GST_OPTION_CFLAGS)"
dnl FIXME: check if LTLIBINTL is needed everywhere
dnl I presume it is given that it contains the symbols that _() stuff maps to
GST_ALL_LIBS="$GLIB_LIBS $LTLIBINTL"
dnl LDFLAGS modifier defining exported symbols from built shared objects
EXPORT_LDFLAGS="-export-symbols-regex [_]*\(gst_\|Gst\|GST_\).*"
GST_ALL_LDFLAGS="$EXPORT_LDFLAGS -no-undefined"
AC_SUBST(GST_ALL_CFLAGS)
AC_SUBST(GST_ALL_LIBS)
AC_SUBST(GST_ALL_LDFLAGS)

dnl GST_OBJ_*
dnl default vars for all internal objects built on libgstreamer
GST_OBJ_CFLAGS="-I\$(top_srcdir)/libs -I\$(top_srcdir) \$(GST_ALL_CFLAGS)"
GST_OBJ_LIBS="\$(top_builddir)/gst/libgstreamer-$GST_MAJORMINOR.la \$(GST_ALL_LIBS)"
GST_OBJ_LDFLAGS="$GST_LT_LDFLAGS $GST_ALL_LDFLAGS"
AC_SUBST(GST_OBJ_CFLAGS)
AC_SUBST(GST_OBJ_LIBS)

dnl GST_PLUGIN_LDFLAGS
dnl LDFLAGS for plugins; include GST_ALL_LDFLAGS
GST_PLUGIN_LDFLAGS="-module -avoid-version $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS, "$GST_PLUGIN_LDFLAGS")

dnl ##################################################
dnl # deps for examples from manual                  #
dnl ##################################################

PKG_CHECK_MODULES(LIBGNOMEUI, libgnomeui-2.0,
                  HAVE_LIBGNOMEUI="yes", HAVE_LIBGNOMEUI="no")
AM_CONDITIONAL(HAVE_LIBGNOMEUI, test "x$HAVE_LIBGNOMEUI" = "xyes")

dnl ##################################################
dnl # Prepare informative messages to display at end #
dnl ##################################################

infomessages=

dnl *** output files ***

dnl libs/ext/Makefile
dnl nothing there yet !
AC_CONFIG_FILES(
Makefile
gst/Makefile
gst/gstconfig.h
gst/gstversion.h
gst/base/Makefile
gst/check/Makefile
gst/indexers/Makefile
gst/elements/Makefile
gst/parse/Makefile
libs/Makefile
libs/gst/Makefile
libs/gst/controller/Makefile
libs/gst/dataprotocol/Makefile
po/Makefile.in
check/Makefile
tests/Makefile
tests/instantiate/Makefile
tests/muxing/Makefile
tests/seeking/Makefile
tests/sched/Makefile
tests/threadstate/Makefile
testsuite/Makefile
testsuite/bytestream/Makefile
testsuite/caps/Makefile
testsuite/cleanup/Makefile
testsuite/controller/Makefile
testsuite/debug/Makefile
testsuite/dlopen/Makefile
testsuite/elements/Makefile
testsuite/indexers/Makefile
testsuite/negotiation/Makefile
testsuite/pad/Makefile
testsuite/parse/Makefile
testsuite/plugin/Makefile
testsuite/refcounting/Makefile
testsuite/schedulers/Makefile
testsuite/states/Makefile
testsuite/threads/Makefile
testsuite/trigger/Makefile
examples/Makefile
examples/controller/Makefile
examples/cutter/Makefile
examples/helloworld/Makefile
examples/launch/Makefile
examples/manual/Makefile
examples/mixer/Makefile
examples/metadata/Makefile
examples/pingpong/Makefile
examples/plugins/Makefile
examples/pwg/Makefile
examples/queue/Makefile
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
docs/pwg/Makefile
docs/xsl/Makefile
docs/version.entities
pkgconfig/Makefile
stamp.h
pkgconfig/gstreamer.pc
pkgconfig/gstreamer-uninstalled.pc
pkgconfig/gstreamer-base.pc
pkgconfig/gstreamer-base-uninstalled.pc
pkgconfig/gstreamer-check.pc
pkgconfig/gstreamer-check-uninstalled.pc
pkgconfig/gstreamer-controller.pc
pkgconfig/gstreamer-controller-uninstalled.pc
pkgconfig/gstreamer-dataprotocol.pc
pkgconfig/gstreamer-dataprotocol-uninstalled.pc
gstreamer.spec
)
AC_CONFIG_COMMANDS(echo "$infomessages", infomessages="$infomessages")
AC_OUTPUT
