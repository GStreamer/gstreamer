AC_PREREQ(2.52)

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT(GStreamer, 0.10.22,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gstreamer)
AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_CVS="no", GST_CVS="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/gst.c])

dnl define the output header for config
AM_CONFIG_HEADER([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl sets host_* variables
AC_CANONICAL_HOST

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR
dnl we override it here for release candidates for a new series
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)
AC_DEFINE_UNQUOTED(GST_MAJORMINOR, "$GST_MAJORMINOR",
  [library major.minor version])

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
dnl sets GST_LT_LDFLAGS
AS_LIBTOOL(GST, 19, 0, 19)

dnl FIXME: this macro doesn't actually work;
dnl the generated libtool script has no support for the listed tags.
dnl So this needs to be fixed first if we want to use this
dnl AS_LIBTOOL_TAGS

AC_LIBTOOL_WIN32_DLL
AM_PROG_LIBTOOL

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I common/m4")

dnl set up gettext
dnl the version check needs to stay here because autopoint greps for it
AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])
AG_GST_GETTEXT([gstreamer-$GST_MAJORMINOR])

dnl *** check for arguments to configure ***

dnl subsystems - can influence other decisions so needs to be high up
dnl we need to AM_CONDITIONAL them here for automake 1.6.x compatibility
AG_GST_CHECK_SUBSYSTEM_DISABLE(GST_DEBUG,[debugging subsystem])
AM_CONDITIONAL(GST_DISABLE_GST_DEBUG, test "x$GST_DISABLE_GST_DEBUG" = "xyes")
AG_GST_CHECK_SUBSYSTEM_DISABLE(LOADSAVE,[pipeline XML load/save])
AM_CONDITIONAL(GST_DISABLE_LOADSAVE, test "x$GST_DISABLE_LOADSAVE" = "xyes")
dnl define a substitution to use in docs/gst/gstreamer.types
if test "x$GST_DISABLE_LOADSAVE" = "xyes"
then
  GST_LOADSAVE_DOC_TYPES="%"
else
  GST_LOADSAVE_DOC_TYPES=
fi
AC_SUBST(GST_LOADSAVE_DOC_TYPES)
AG_GST_CHECK_SUBSYSTEM_DISABLE(PARSE,[command-line parser])
AM_CONDITIONAL(GST_DISABLE_PARSE, test "x$GST_DISABLE_PARSE" = "xyes")
if test "x$GST_DISABLE_PARSE" = xyes; then
  AC_DEFINE(GST_DISABLE_PARSE, 1,
    [Define if pipeline parsing code is disabled])
fi
AG_GST_CHECK_SUBSYSTEM_DISABLE(OPTION_PARSING,[option parsing when gst_init])
AM_CONDITIONAL(GST_DISABLE_OPTION_PARSING, test "x$GST_DISABLE_OPTION_PARSING" = "xyes")
if test "x$GST_DISABLE_OPTION_PARSING" = xyes; then
  AC_DEFINE(GST_DISABLE_OPTION_PARSING, 1,
    [Define if option parsing is disabled])
fi
AG_GST_CHECK_SUBSYSTEM_DISABLE(TRACE,[tracing subsystem])
AM_CONDITIONAL(GST_DISABLE_TRACE, test "x$GST_DISABLE_TRACE" = "xyes")
AG_GST_CHECK_SUBSYSTEM_DISABLE(ALLOC_TRACE,[allocation tracing])
AM_CONDITIONAL(GST_DISABLE_ALLOC_TRACE, test "x$GST_DISABLE_ALLOC_TRACE" = "xyes")
AG_GST_CHECK_SUBSYSTEM_DISABLE(REGISTRY,[plugin registry])
AM_CONDITIONAL(GST_DISABLE_REGISTRY, test "x$GST_DISABLE_REGISTRY" = "xyes")
dnl define a substitution to use in docs/gst/gstreamer.types
if test "x$GST_DISABLE_REGISTRY" = "xyes"
then
  GST_REGISTRY_DOC_TYPES="%"
else
  GST_REGISTRY_DOC_TYPES=
fi
AC_SUBST(GST_REGISTRY_DOC_TYPES)
AG_GST_CHECK_SUBSYSTEM_DISABLE(NET,[network distribution])
AM_CONDITIONAL(GST_DISABLE_NET, test "x$GST_DISABLE_NET" = "xyes")
AG_GST_CHECK_SUBSYSTEM_DISABLE(PLUGIN,[plugin])
AM_CONDITIONAL(GST_DISABLE_PLUGIN, test "x$GST_DISABLE_PLUGIN" = "xyes")

AG_GST_ARG_DEBUG
AG_GST_ARG_PROFILING
AG_GST_ARG_VALGRIND
AG_GST_ARG_GCOV

AG_GST_ARG_EXAMPLES

AG_GST_ARG_WITH_PKG_CONFIG_PATH
AG_GST_ARG_WITH_PACKAGE_NAME
AG_GST_ARG_WITH_PACKAGE_ORIGIN

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

dnl Use binary registry
AC_ARG_ENABLE(binary-registry,
  AC_HELP_STRING([--disable-binary-registry],
    [disable binary registry]),
  [
    case "${enableval}" in
      yes) USE_BINARY_REGISTRY=yes ;;
      no)  USE_BINARY_REGISTRY=no ;;
      *)   AC_MSG_ERROR(bad value ${enableval} for --disable-binary-registry) ;;
    esac
  ],
  [USE_BINARY_REGISTRY=yes]) dnl Default value

if test "x$USE_BINARY_REGISTRY" = xyes; then
  dnl for config.h:
  AC_DEFINE(USE_BINARY_REGISTRY, 1,
    [Define if we should use binary registry instead xml registry])
fi
AM_CONDITIONAL(USE_BINARY_REGISTRY, test "x$USE_BINARY_REGISTRY" = "xyes")

dnl *** checks for platform ***

dnl * hardware/architecture *

dnl common/m4/gst-arch.m4
dnl check CPU type
AG_GST_ARCH
dnl substitution for win32/common/config.h
HOST_CPU=$host_cpu
AC_SUBST(HOST_CPU)

dnl common/m4/gst-arch.m4
dnl check for unaligned access
AG_GST_UNALIGNED_ACCESS
dnl create a configure variable for gst/gstconfig.h
if test x${as_cv_unaligned_access} = xyes ; then
  GST_HAVE_UNALIGNED_ACCESS_DEFINE="#define GST_HAVE_UNALIGNED_ACCESS 1"
else
  GST_HAVE_UNALIGNED_ACCESS_DEFINE="#define GST_HAVE_UNALIGNED_ACCESS 0"
fi
AC_SUBST(GST_HAVE_UNALIGNED_ACCESS_DEFINE)

dnl check for platform specific settings
case $host_os in
     rhapsody*)
	AC_DEFINE_UNQUOTED(GST_EXTRA_MODULE_SUFFIX, [".dylib"], [Extra platform specific plugin suffix])
        ;;
     darwin*)
	AC_DEFINE_UNQUOTED(GST_EXTRA_MODULE_SUFFIX, [".dylib"], [Extra platform specific plugin suffix])
        AC_DEFINE_UNQUOTED(GST_HAVE_UNSAFE_FORK, 1, [Defined when registry scanning through fork is unsafe])
	;;
     cygwin* | mingw* | msvc* | mks*)
        dnl HAVE_WIN32 currently means "disable POSIXisms".
        AC_DEFINE_UNQUOTED(HAVE_WIN32, 1, [Defined if compiling for Windows])
        ;;
     *)
	;;
esac

dnl * software *

dnl check for large file support
dnl affected plugins must include config.h
AC_SYS_LARGEFILE

dnl *** checks for programs ***

dnl find a compiler
AC_PROG_CC

dnl check if the compiler supports '-c' and '-o' options
AM_PROG_CC_C_O

dnl find an assembler
AM_PROG_AS

dnl Perl is used in building documentation and in the version checks
AC_PATH_PROG(PERL_PATH, perl, no)
if test x$PERL_PATH = xno; then
  AC_MSG_ERROR(Could not find perl)
fi

dnl we require flex and bison for building the parser
if test "x$GST_DISABLE_PARSE" != xyes; then
  AG_GST_BISON_CHECK
  AG_GST_FLEX_CHECK
fi

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for documentation tools
AG_GST_DOCBOOK_CHECK
GTK_DOC_CHECK([1.3])
AS_PATH_PYTHON([2.1])
AG_GST_PLUGIN_DOCS([1.3],[2.1])

dnl *** checks for libraries ***

dnl check for libm, for sin()
AC_CHECK_LIBM
AC_SUBST(LIBM)

dnl *** checks for header files ***

dnl check if we have ANSI C header files
AC_HEADER_STDC

dnl Check for ucontext.h
AC_CHECK_HEADERS([ucontext.h])

dnl Check for sys/socket.h
AC_CHECK_HEADERS([sys/socket.h], HAVE_SYS_SOCKET_H=yes)
AM_CONDITIONAL(HAVE_SYS_SOCKET_H, test "x$HAVE_SYS_SOCKET_H" = "xyes")

dnl check for sys/times.h for tests/examples/adapter/
AC_CHECK_HEADERS([sys/times.h], HAVE_SYS_TIMES_H=yes)
AC_CHECK_HEADERS([unistd.h], HAVE_UNISTD_H=yes)
AM_CONDITIONAL(HAVE_SYS_TIMES_H_AND_UNISTD_H, test "x$HAVE_SYS_TIMES_H" = "xyes" -a "x$HAVE_UNISTD_H" = "xyes")

dnl Check for process.h for getpid() on win32
AC_CHECK_HEADERS([process.h])

dnl Check for sys/utsname.h for uname
AC_CHECK_HEADERS([sys/utsname.h])

dnl Check for stdio_ext.f for __fbufsize
AC_CHECK_HEADERS([stdio_ext.h])

dnl Check for valgrind.h
dnl separate from HAVE_VALGRIND because you can have the program, but not
dnl the dev package
AC_CHECK_HEADERS([valgrind/valgrind.h], HAVE_VALGRIND_H=yes)

dnl used in gst/gstpoll.c
AC_CHECK_HEADERS([winsock2.h], HAVE_WINSOCK2_H=yes)
AM_CONDITIONAL(HAVE_WINSOCK2_H, test "x$HAVE_WINSOCK2_H" = "xyes")
if test "x$HAVE_WINSOCK2_H" = "xyes"; then
  WIN32_LIBS="-lws2_32"
  AC_SUBST(WIN32_LIBS)
fi

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl make sure we can use "inline" from C code
AC_C_INLINE

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

dnl *** checks for library functions ***

AC_CHECK_FUNCS([sigaction])

dnl we use fork in the registry code
AC_CHECK_FUNCS([fork])

dnl check for fseeko()
AC_FUNC_FSEEKO
dnl check for ftello()
AC_CHECK_FUNCS([ftello])

AC_CHECK_FUNCS([fgetpos])
AC_CHECK_FUNCS([fsetpos])

dnl check for poll(), ppoll() and pselect()
AC_CHECK_FUNCS([poll])
AC_CHECK_FUNCS([ppoll])
AC_CHECK_FUNCS([pselect])

dnl check for mmap()
AC_FUNC_MMAP
AM_CONDITIONAL(HAVE_MMAP, test "x$ac_cv_func_mmap_fixed_mapped" = "xyes")

dnl Check for POSIX timers
AC_CHECK_FUNCS(clock_gettime, [], [
  AC_CHECK_LIB(rt, clock_gettime, [
    AC_DEFINE(HAVE_CLOCK_GETTIME, 1)
    LIBS="$LIBS -lrt"
  ])
])

AC_CACHE_CHECK(for posix timers, gst_cv_posix_timers,
    AC_TRY_COMPILE([
#include <time.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
    ], [
#if !defined(_POSIX_TIMERS) || _POSIX_TIMERS < 0 || !defined(CLOCK_REALTIME)
#error Either _POSIX_TIMERS or CLOCK_REALTIME not defined
#endif
      return 0;
    ], [
      gst_cv_posix_timers=yes
    ], [
      gst_cv_posix_timers=no
    ])
)

if test "$gst_cv_posix_timers" = "yes"; then
  AC_DEFINE(HAVE_POSIX_TIMERS,1,[Have posix timers])
  GST_HAVE_POSIX_TIMERS_DEFINE="#define GST_HAVE_POSIX_TIMERS 1"
else
  GST_HAVE_POSIX_TIMERS_DEFINE="#define GST_HAVE_POSIX_TIMERS 0"
fi
AC_SUBST(GST_HAVE_POSIX_TIMERS_DEFINE)
AM_CONDITIONAL(GST_HAVE_POSIX_TIMERS, test "$gst_cv_posix_timers" = "yes")

AC_CACHE_CHECK(for monotonic clock, gst_cv_monotonic_clock,
    AC_TRY_COMPILE([
#include <time.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
    ], [
#if !defined(_POSIX_MONOTONIC_CLOCK) || _POSIX_MONOTONIC_CLOCK < 0 || !defined(CLOCK_MONOTONIC)
#error Either _POSIX_MONOTONIC_CLOCK or CLOCK_MONOTONIC not defined
#endif
      return 0;
    ], [
      gst_cv_monotonic_clock=yes
    ], [
      gst_cv_monotonic_clock=no
    ])
)

if test "$gst_cv_monotonic_clock" = "yes"; then
  AC_DEFINE(HAVE_MONOTONIC_CLOCK,1,[Have a monotonic clock])
  GST_HAVE_MONOTONIC_CLOCK_DEFINE="#define GST_HAVE_MONOTONIC_CLOCK 1"
else
  GST_HAVE_MONOTONIC_CLOCK_DEFINE="#define GST_HAVE_MONOTONIC_CLOCK 0"
fi
AC_SUBST(GST_HAVE_MONOTONIC_CLOCK_DEFINE)
AM_CONDITIONAL(GST_HAVE_MONOTONIC_CLOCK, test "$gst_cv_monotonic_clock" = "yes")

dnl Check for a way to display the function name in debug output
AG_GST_CHECK_FUNCTION

dnl test for register_printf_function
AC_CHECK_FUNC(register_printf_function,
  [
    GST_PRINTF_EXTENSION_POINTER_FORMAT_DEFINE="#define GST_PTR_FORMAT \"P\""
    GST_PRINTF_EXTENSION_SEGMENT_FORMAT_DEFINE="#define GST_SEGMENT_FORMAT \"Q\""
    GST_USING_PRINTF_EXTENSION_DEFINE="#define GST_USING_PRINTF_EXTENSION"
    AC_DEFINE(HAVE_PRINTF_EXTENSION, 1,
      [Defined if we have register_printf_function ()])
  ], [
    GST_PRINTF_EXTENSION_POINTER_FORMAT_DEFINE="#define GST_PTR_FORMAT \"p\""
    GST_PRINTF_EXTENSION_SEGMENT_FORMAT_DEFINE="#define GST_SEGMENT_FORMAT \"p\""
    GST_USING_PRINTF_EXTENSION_DEFINE="#undef GST_USING_PRINTF_EXTENSION"
  ]
)
AC_SUBST(GST_PRINTF_EXTENSION_POINTER_FORMAT_DEFINE)
AC_SUBST(GST_PRINTF_EXTENSION_SEGMENT_FORMAT_DEFINE)
AC_SUBST(GST_USING_PRINTF_EXTENSION_DEFINE)

dnl test if we have dladdr(); we use it for debugging; see gst/gstinfo.c
save_cflags="$CFLAGS"
CFLAGS="$CFLAGS -D_GNU_SOURCE"
AC_CHECK_LIB(dl, dladdr,
   AC_DEFINE(HAVE_DLADDR, 1, [Defined if we have dladdr ()])
   LIBS="$LIBS -ldl")
CFLAGS="$save_cflags"

dnl check for inet_aton()
save_libs="$LIBS"
AC_CHECK_FUNC(inet_aton, ,
   AC_CHECK_LIB(nsl, inet_aton, ,
      AC_CHECK_LIB(socket, inet_aton, ,
         AC_CHECK_LIB(resolv, inet_aton, ,[AC_DEFINE(NO_INET_ATON, 1, [Define if you have no native inet_aton() function.])]))))
INET_ATON_LIBS=$LIBS
AC_SUBST(INET_ATON_LIBS)
LIBS="$save_libs"

dnl *** checks for dependency libraries ***

dnl GLib
GLIB_REQ=2.12

AG_GST_GLIB_CHECK([$GLIB_REQ])

dnl FIXME: 0.11: Guess we need to keep this around until 0.11
GST_HAVE_GLIB_2_8_DEFINE="#define GST_HAVE_GLIB_2_8 1"
AC_SUBST(GST_HAVE_GLIB_2_8_DEFINE)

dnl Check for glib2 without extra fat, useful for the unversioned tool frontends
PKG_CHECK_MODULES(GLIB_ONLY, glib-2.0 >= $GLIB_REQ)

dnl Check for documentation xrefs
GLIB_PREFIX="`$PKG_CONFIG --variable=prefix glib-2.0`"
AC_SUBST(GLIB_PREFIX)

dnl libxml 2 is optional in some cases
if test "x$GST_DISABLE_LOADSAVE" = "xyes" && \
   ( test "x$GST_DISABLE_REGISTRY" = "xyes" || \
   test "x$USE_BINARY_REGISTRY" = "xyes" )
then
  AC_MSG_NOTICE([XML registry and load/save are disabled, not checking for libxml2])
  GST_DISABLE_XML_DEFINE="#define GST_DISABLE_XML 1"
else
  dnl check for libxml2 with minimum req version
  AG_GST_LIBXML2_CHECK(2.4.9)
  GST_DISABLE_XML_DEFINE="/* #undef GST_DISABLE_XML */"
fi
AC_SUBST(GST_DISABLE_XML_DEFINE)

dnl check for "check", unit testing library/header
if test "$cross_compiling" != yes; then
  AM_PATH_CHECK(0.9.2,
      HAVE_CHECK=yes,
      HAVE_CHECK=no)
else
  HAVE_CHECK=no
fi
AM_CONDITIONAL(HAVE_CHECK, test "x$HAVE_CHECK" = "xyes")

dnl pkg-config check that for libcheck that works for cross-compiling
dnl but not normal compilation on Fedora & Ubuntu:
dnl PKG_CHECK_MODULES(CHECK, check >= 0.9.2,
dnl   AM_CONDITIONAL(HAVE_CHECK, true),
dnl   AM_CONDITIONAL(HAVE_CHECK, false)
dnl )

dnl *** set variables based on configure arguments

dnl set license and copyright notice
GST_LICENSE="LGPL"
AC_DEFINE_UNQUOTED(GST_LICENSE, "$GST_LICENSE", [GStreamer license])
AC_SUBST(GST_LICENSE)

dnl define LIBDIR, GST_DATADIR so we can inform people where we live
AS_AC_EXPAND(LIBDIR, $libdir)
AC_DEFINE_UNQUOTED(LIBDIR, "$LIBDIR", [library dir])
AS_AC_EXPAND(DATADIR, $datadir)
AC_DEFINE_UNQUOTED(GST_DATADIR, "$DATADIR", [data dir])

dnl set location of plugin directory
AG_GST_SET_PLUGINDIR

dnl FIXME: add LIBXML_PKG here if we use it
GST_PKG_DEPS="glib-2.0, gobject-2.0, gmodule-no-export-2.0, gthread-2.0"
AC_SUBST(GST_PKG_DEPS)

dnl define an ERROR_CFLAGS Makefile variable
AG_GST_SET_ERROR_CFLAGS($GST_CVS)

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT($GST_CVS)

dnl *** finalize CFLAGS, LDFLAGS, LIBS

dnl Overview:
dnl GST_OPTION_CFLAGS:  common cflags for profiling, debugging, errors, ...
dnl GST_ALL_*:          vars shared by all built objects
dnl GST_LIB_LDFLAGS:    additional linker flags for all libaries
dnl GST_OBJ_*:          additional vars to link to the core library
dnl			include GST_ALL_*
dnl GST_LT_LDFLAGS:	library versioning of our libraries
dnl GST_PLUGIN_LDFLAGS: flags to be used for all plugins

dnl GST_OPTION_CFLAGS
if test "x$USE_DEBUG" = xyes; then
   PROFILE_CFLAGS="-g"
fi
AC_SUBST(PROFILE_CFLAGS)

# GST_DISABLE_DEPRECATED: hide the visibility of deprecated
# functionality from the API that gstreamer uses
# GST_REMOVE_DEPRECATED: don't compile deprecated functionality (breaks ABI)
if test "x$PACKAGE_VERSION_NANO" = "x1"; then
  dnl Define _only_ during CVS (not pre-releases or releases)
  DEPRECATED_CFLAGS="-DGST_DISABLE_DEPRECATED"
else
  DEPRECATED_CFLAGS=""
fi
AC_SUBST(DEPRECATED_CFLAGS)

dnl every flag in GST_OPTION_CFLAGS can be overridden at make time via e.g.
dnl make DEPRECATED_CFLAGS=''
GST_OPTION_CFLAGS="\$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
AC_SUBST(GST_OPTION_CFLAGS)

dnl GST_ALL_*
dnl vars common to for all internal objects (core libs, elements, applications)
dnl CFLAGS:
dnl - XML needs to be added because xmlPtr is used in core
dnl - src and build dirs need to be added because every piece that gets built
dnl   will need the GStreamer source and generated headers
dnl LIBS: XML doesn't need to be added because we don't explicitly use symbols
dnl       from LibXML except for in the core library
GST_ALL_CFLAGS="-I\$(top_srcdir)/libs -I\$(top_srcdir) -I\$(top_builddir) $GLIB_CFLAGS $XML_CFLAGS \$(GST_OPTION_CFLAGS)"

dnl FIXME: check if LTLIBINTL is needed everywhere
dnl I presume it is given that it contains the symbols that _() stuff maps to
GST_ALL_LIBS="$GLIB_LIBS $LTLIBINTL \$(GCOV_LIBS)"

dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_ALL_LDFLAGS="-no-undefined"

AC_SUBST(GST_ALL_CFLAGS)
AC_SUBST(GST_ALL_LIBS)
AC_SUBST(GST_ALL_LDFLAGS)

dnl GST_LIB_LDFLAGS
dnl linker flags shared by all libraries
dnl LDFLAGS modifier defining exported symbols from built libraries
GST_LIB_LDFLAGS="-export-symbols-regex \^[_]*\(gst_\|Gst\|GST_\).*"
AC_SUBST(GST_LIB_LDFLAGS)
dnl Version of the linker flags specifically for libgstcheck to support the boonky extra symbols it exports.
GST_CHECK_LIB_LDFLAGS="-export-symbols-regex \^\([_]*\(gst_\|Gst\|GST_\).*\|check_\(debug\|mutex\|cond\)\|buffers\|mutex\|start_cond\|sync_cond\|thread_list\)$"
dnl HACK: add non-portable --export-dynamic if we have GNU ld (needed on my debian stable, tpm)
if test "x$ac_cv_prog_gnu_ld" = "xyes" -o "x$acl_cv_prog_gnu_ld" = "xyes" ; then
  GST_CHECK_LIB_LDFLAGS="-Wl,--export-dynamic $GST_CHECK_LIB_LDFLAGS"
fi
AC_SUBST(GST_CHECK_LIB_LDFLAGS)

dnl GST_OBJ_*
dnl default vars for all internal objects built on libgstreamer
dnl includes GST_ALL_*
GST_OBJ_CFLAGS="\$(GST_ALL_CFLAGS)"
GST_OBJ_LIBS="\$(top_builddir)/gst/libgstreamer-$GST_MAJORMINOR.la \$(GST_ALL_LIBS)"
AC_SUBST(GST_OBJ_CFLAGS)
AC_SUBST(GST_OBJ_LIBS)

dnl GST_PLUGIN_LDFLAGS
dnl LDFLAGS for plugins; includes GST_ALL_LDFLAGS
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_desc\$\$' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS, "$GST_PLUGIN_LDFLAGS")

AC_CONFIG_FILES(
Makefile
gst/Makefile
gst/gstconfig.h
gst/gstversion.h
gst/parse/Makefile
libs/Makefile
libs/gst/Makefile
libs/gst/base/Makefile
libs/gst/check/Makefile
libs/gst/controller/Makefile
libs/gst/dataprotocol/Makefile
libs/gst/net/Makefile
plugins/Makefile
plugins/indexers/Makefile
plugins/elements/Makefile
po/Makefile.in
tests/Makefile
tests/benchmarks/Makefile
tests/check/Makefile
tests/misc/Makefile
tests/examples/Makefile
tests/examples/adapter/Makefile
tests/examples/controller/Makefile
tests/examples/helloworld/Makefile
tests/examples/launch/Makefile
tests/examples/manual/Makefile
tests/examples/metadata/Makefile
tests/examples/queue/Makefile
tests/examples/typefind/Makefile
tests/examples/xml/Makefile
tools/Makefile
common/Makefile
common/m4/Makefile
docs/Makefile
docs/design/Makefile
docs/faq/Makefile
docs/gst/Makefile
docs/gst/gstreamer.types
docs/libs/Makefile
docs/plugins/Makefile
docs/manual/Makefile
docs/pwg/Makefile
docs/slides/Makefile
docs/xsl/Makefile
docs/version.entities
win32/common/config.h
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
pkgconfig/gstreamer-net.pc
pkgconfig/gstreamer-net-uninstalled.pc
gst-element-check.m4
gstreamer.spec
)

AC_OUTPUT

dnl negate for output
if test "x${GST_DISABLE_LOADSAVE}" = "xno"; then enable_loadsave="yes"; fi
if test "x${GST_DISABLE_PARSE}" = "xno"; then enable_parse="yes"; fi
if test "x${GST_DISABLE_OPTION_PARSING}" = "xno"; then enable_option_parsing="yes"; fi
if test "x${GST_DISABLE_TRACE}" = "xno"; then enable_trace="yes"; fi
if test "x${GST_DISABLE_ALLOC_TRACE}" = "xno"; then enable_alloc_trace="yes"; fi
if test "x${GST_DISABLE_PLUGIN}" = "xno"; then enable_plugin="yes"; fi
if test "x${GST_DISABLE_REGISTRY}" = "xno"; then enable_registry="yes"; fi
if test "x${GST_DISABLE_NET}" = "xno"; then enable_net="yes"; fi

echo "

Configuration
	Version                    : ${VERSION}
	Source code location       : ${srcdir}
	Prefix                     : ${prefix}
	Compiler                   : ${CC}
	Package name               : ${GST_PACKAGE_NAME}
	Package origin             : ${GST_PACKAGE_ORIGIN}

	Documentation (manuals)    : ${enable_docbook}
	Documentation (API)        : ${enable_gtk_doc}

	Pipeline XML load/save     : ${enable_loadsave}
	Command-line parser        : ${enable_parse}
	Option parsing in gst_init : ${enable_option_parsing}
	Tracing subsystem          : ${enable_trace}
	Allocation tracing         : ${enable_alloc_trace}
	Plugin registry            : ${enable_registry}
	Plugin support	           : ${enable_plugin}
	Network support            : ${enable_net}

	Debug                      : ${USE_DEBUG}
	Profiling                  : ${USE_PROFILING}

	Building examples          : ${BUILD_EXAMPLES}
	Building test apps         : ${BUILD_TESTS}
	Building tests that fail   : ${BUILD_FAILING_TESTS}
"
