AC_PREREQ(2.60)

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, git and prerelease does Werror too
AC_INIT(GStreamer, 0.10.36,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gstreamer)
AG_GST_INIT

dnl initialize automake (we require GNU make)
AM_INIT_AUTOMAKE([-Wno-portability 1.10])

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_GIT="no", GST_GIT="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/gst.c])

dnl define the output header for config
AM_CONFIG_HEADER([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl sets host_* variables
AC_CANONICAL_HOST

dnl use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST(AM_DEFAULT_VERBOSITY)])

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
AS_LIBTOOL(GST, 30, 0, 30)

dnl FIXME: this macro doesn't actually work;
dnl the generated libtool script has no support for the listed tags.
dnl So this needs to be fixed first if we want to use this
dnl AS_LIBTOOL_TAGS

AC_LIBTOOL_WIN32_DLL
AM_PROG_LIBTOOL

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal (keep in sync with Makefile.am)
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

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

AG_GST_PKG_CONFIG_PATH

AG_GST_SET_PACKAGE_RELEASE_DATETIME_WITH_NANO([$PACKAGE_VERSION_NANO],
  ["${srcdir}/gstreamer.doap"],
  [$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR.$PACKAGE_VERSION_MICRO])

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
AG_GST_PLATFORM

dnl * software *

dnl check for large file support
dnl affected plugins must include config.h
AC_SYS_LARGEFILE

dnl *** checks for programs ***

dnl find a compiler
AC_PROG_CC
AC_PROG_CC_STDC

dnl check if the compiler supports '-c' and '-o' options
AM_PROG_CC_C_O

dnl find an assembler
AM_PROG_AS

dnl determine if c++ is available on this system
AC_PROG_CXX
dnl CXX may be set to some default even if no c++ compiler is available
dnl (thanks autotools!), so just try to compile some c++ code to make sure
AC_LANG_PUSH([C++])
AC_TRY_COMPILE([ class Foo { int bar; };], , working_cxx=yes, working_cxx=no)
AC_LANG_POP([C++])
AC_MSG_NOTICE([working c++ compiler found: $working_cxx])
AM_CONDITIONAL(HAVE_CXX, test "x$working_cxx" = "xyes")

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

dnl check for gobject-introspection
GOBJECT_INTROSPECTION_CHECK([0.6.8])

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

dnl check for pthreads
AC_CHECK_HEADERS([pthread.h], HAVE_PTHREAD_H=yes)
AM_CONDITIONAL(HAVE_PTHREAD_H, test "x$HAVE_PTHREAD_H" = "xyes")

dnl check for sys/prctl for setting thread name on Linux
AC_CHECK_HEADERS([sys/prctl.h], HAVE_SYS_PRCTL_H=yes)

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

dnl check for GMP/GSL, used by the gst_util_uint64_scale unit test only
if test "x$BUILD_TESTS" = "xyes"; then
  AG_GST_CHECK_LIBHEADER(GMP, gmp,
                         __gmpz_init_set_d, ,
                         gmp.h,
                         GMP_LIBS="-lgmp"
                         AC_SUBST(GMP_LIBS)
                         AC_DEFINE(HAVE_GMP, [1],[Have GMP library]))
  AG_GST_CHECK_LIBHEADER(GSL, gsl,
                         gsl_rng_uniform_int, -lgslcblas,
                         gsl/gsl_rng.h,
                         GSL_LIBS="-lgsl -lgslcblas"
                         AC_SUBST(GSL_LIBS)
                         AC_DEFINE(HAVE_GSL, [1],[Have GSL library]))
fi

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl check if the compiler supports __uint128_t (gcc)
dnl Actually check for 128-bit division, since that's what we use
dnl uint128_t for.
AC_CACHE_CHECK(for __uint128_t, gst_cv_uint128_t,
    AC_TRY_LINK([ ], [
      static __uint128_t v1 = 100;
      static __uint128_t v2 = 10;
      static __uint128_t u;
      u = v1 / v2;
    ], [
      gst_cv_uint128_t=yes
    ], [
      gst_cv_uint128_t=no
    ])
)
if test x$gst_cv_uint128_t = xyes; then
  AC_DEFINE(HAVE_UINT128_T, 1, [Have __uint128_t type])
fi

dnl *** checking for tm_gmtoff ***
AC_MSG_CHECKING([for tm_gmtoff])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
	#include <time.h>
	int main(void) {
	  struct tm t;
	  t.tm_gmtoff = 0;
	  exit(0);
	}]])],
  [have_tm_gmtoff=yes
   AC_DEFINE(HAVE_TM_GMTOFF,1,[Have tm_gmtoff field in struct tm])],
  [have_tm_gmtoff=no],
  [have_tm_gmtoff="no (cross compiling)"])
AC_MSG_RESULT($have_tm_gmtoff)


dnl *** checks for library functions ***

AC_CHECK_FUNCS([gmtime_r])
AC_CHECK_FUNCS([localtime_r])
AC_CHECK_FUNCS([sigaction])

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

AC_CHECK_HEADERS([sys/poll.h])

dnl ****************************************
dnl *** GLib POLL* compatibility defines ***
dnl ****************************************

AC_MSG_CHECKING([for broken poll])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
	#include <stdlib.h>
	#include <fcntl.h>
	#include <poll.h>
	#ifdef HAVE_SYS_POLL_H
	#include <sys/poll.h>
	#endif
	int main(void) {
	  struct pollfd fds[1];
	  int fd;
	  fd = open("/dev/null", 1);
	  fds[0].fd = fd;
	  fds[0].events = POLLIN;
	  fds[0].revents = 0;
	  if (poll(fds, 1, 0) < 0 || (fds[0].revents & POLLNVAL) != 0) {
		exit(1);  /* Does not work for devices -- fail */
	  }
	  exit(0);
	}]])],
  [broken_poll=no],
  [broken_poll=yes
   AC_DEFINE(BROKEN_POLL,1,[poll doesn't work on devices])],
  [broken_poll="no (cross compiling)"])
AC_MSG_RESULT($broken_poll)

dnl check for mmap()
AC_FUNC_MMAP
AM_CONDITIONAL(HAVE_MMAP, test "x$ac_cv_func_mmap_fixed_mapped" = "xyes")

dnl check for posix_memalign(), getpagesize()
AC_CHECK_FUNCS([posix_memalign])
AC_CHECK_FUNCS([getpagesize])

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

dnl test for register_printf_specifier or register_printf_function
AC_CHECK_FUNCS([register_printf_specifier register_printf_function],
    [HAVE_PRINTF_EXTENSION=yes])

if test "$HAVE_PRINTF_EXTENSION" = yes; then
  GST_PRINTF_EXTENSION_POINTER_FORMAT_DEFINE="#define GST_PTR_FORMAT \"P\""
  GST_PRINTF_EXTENSION_SEGMENT_FORMAT_DEFINE="#define GST_SEGMENT_FORMAT \"Q\""
  GST_USING_PRINTF_EXTENSION_DEFINE="#define GST_USING_PRINTF_EXTENSION"
  AC_DEFINE(HAVE_PRINTF_EXTENSION, 1,
      [Defined if we have printf specifier extensions available])
else
    GST_PRINTF_EXTENSION_POINTER_FORMAT_DEFINE="#define GST_PTR_FORMAT \"p\""
    GST_PRINTF_EXTENSION_SEGMENT_FORMAT_DEFINE="#define GST_SEGMENT_FORMAT \"p\""
    GST_USING_PRINTF_EXTENSION_DEFINE="#undef GST_USING_PRINTF_EXTENSION"
fi
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

dnl *** checks for socket and nsl libraries ***
AC_CHECK_FUNC(socket,,[AC_CHECK_LIB(socket,socket)])
AC_CHECK_FUNC(gethostbyname,,[AC_CHECK_LIB(nsl,gethostbyname)])

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
GLIB_REQ=2.24

AG_GST_GLIB_CHECK([$GLIB_REQ])

dnl FIXME: 0.11: Guess we need to keep this around until 0.11
GST_HAVE_GLIB_2_8_DEFINE="#define GST_HAVE_GLIB_2_8 1"
AC_SUBST(GST_HAVE_GLIB_2_8_DEFINE)

dnl Check for glib2 without extra fat, useful for the unversioned tool frontends
PKG_CHECK_MODULES(GLIB_ONLY, glib-2.0 >= $GLIB_REQ)

dnl Check for documentation xrefs
GLIB_PREFIX="`$PKG_CONFIG --variable=prefix glib-2.0`"
AC_SUBST(GLIB_PREFIX)

dnl libxml2 is only needed for xml loading/saving of pipelines these days
if test "x$GST_DISABLE_LOADSAVE" = "xyes"
then
  AC_MSG_NOTICE([XML registry and load/save are disabled, not checking for libxml2])
  GST_DISABLE_XML_DEFINE="#define GST_DISABLE_XML 1"
else
  dnl check for libxml2 with minimum req version
  AG_GST_LIBXML2_CHECK(2.4.9)
  GST_DISABLE_XML_DEFINE="/* #undef GST_DISABLE_XML */"
fi
AC_SUBST(GST_DISABLE_XML_DEFINE)

dnl building of unit test libraries
AC_ARG_ENABLE(check,
  AC_HELP_STRING([--disable-check], [disable building unit test libraries]),
  [
    case "${enableval}" in
      yes) BUILD_CHECK=yes ;;
      no)  BUILD_CHECK=no ;;
      *)   AC_MSG_ERROR(bad value ${enableval} for --disable-check) ;;
    esac
  ], [
    dnl Default value
    case $host_os in
      mingw* | msvc* | mks*) BUILD_CHECK=no ;;
      *) BUILD_CHECK=yes ;;
    esac
])
dnl bit of a misnomer, but keep the conditional named like this so we don't
dnl have to change too much elsewhere
AM_CONDITIONAL(HAVE_CHECK, test "x$BUILD_CHECK" = "xyes")

dnl configure the desired buffer alignment
AC_ARG_WITH([buffer-alignment],
  AS_HELP_STRING([--with-buffer-alignment],[8,N,malloc,pagesize (default is 32)]),
  [
    if test "x$withval" = "xyes"
    then
      AC_DEFINE(BUFFER_ALIGNMENT, 32, [Buffer alignment to use])
    else
      case "${withval}" in
        malloc) AC_DEFINE(BUFFER_ALIGNMENT_MALLOC, 1, [Buffer alignment by malloc default]) ;;
        pagesize)  AC_DEFINE(BUFFER_ALIGNMENT_PAGESIZE, 1, [Buffer alignment by pagesize]) ;;
        *)   AC_DEFINE_UNQUOTED(BUFFER_ALIGNMENT, ${withval}, [Buffer alignment to use]) ;;
      esac
    fi
  ], [
    AC_DEFINE(BUFFER_ALIGNMENT_MALLOC, 1, [Buffer alignment by malloc default]) 
  ]
)

dnl Check for -Bsymbolic-functions linker flag used to avoid
dnl intra-library PLT jumps, if available.
AC_ARG_ENABLE(Bsymbolic,
              [AC_HELP_STRING([--disable-Bsymbolic],
                              [avoid linking with -Bsymbolic])],,
              [SAVED_LDFLAGS="${LDFLAGS}"
               AC_MSG_CHECKING([for -Bsymbolic-functions linker flag])
               LDFLAGS=-Wl,-Bsymbolic-functions
               AC_TRY_LINK([], [int main (void) { return 0; }],
                           AC_MSG_RESULT(yes)
                           enable_Bsymbolic=yes,
                           AC_MSG_RESULT(no)
                           enable_Bsymbolic=no)
               LDFLAGS="${SAVED_LDFLAGS}"])


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

dnl make sure it doesn't complain about unused variables if debugging is disabled
NO_WARNINGS=""
if test "x${GST_DISABLE_GST_DEBUG}" = "xyes"; then
  NO_WARNINGS="-Wno-unused"
fi

dnl define an ERROR_CFLAGS Makefile variable
AG_GST_SET_ERROR_CFLAGS($GST_GIT, [-Wmissing-declarations -Wmissing-prototypes -Wredundant-decls -Wundef -Wwrite-strings -Wformat-nonliteral -Wformat-security -Wold-style-definition -Winit-self -Wmissing-include-dirs -Waddress -Waggregate-return -Wno-multichar -Wnested-externs $NO_WARNINGS])

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT($GST_GIT)

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
  dnl Define _only_ when compiling from git (not for pre-releases or releases)
  DEPRECATED_CFLAGS="-DGST_DISABLE_DEPRECATED"
else
  DEPRECATED_CFLAGS=""
fi
AC_SUBST(DEPRECATED_CFLAGS)

dnl every flag in GST_OPTION_CFLAGS can be overridden at make time via e.g.
dnl make DEPRECATED_CFLAGS=''
GST_OPTION_CFLAGS="\$(WARNING_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
GST_OPTION_CXXFLAGS="\$(WARNING_CXXFLAGS) \$(ERROR_CXXFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
AC_SUBST(GST_OPTION_CFLAGS)
AC_SUBST(GST_OPTION_CXXFLAGS)

dnl GST_ALL_*
dnl vars common to for all internal objects (core libs, elements, applications)
dnl CFLAGS:
dnl - XML needs to be added because xmlPtr is used in core
dnl - src and build dirs need to be added because every piece that gets built
dnl   will need the GStreamer source and generated headers
dnl LIBS: XML doesn't need to be added because we don't explicitly use symbols
dnl       from LibXML except for in the core library
GST_ALL_CXXFLAGS="-I\$(top_srcdir)/libs -I\$(top_srcdir) -I\$(top_builddir)/libs -I\$(top_builddir) $GLIB_CFLAGS \$(GLIB_EXTRA_CFLAGS) $XML_CFLAGS \$(GST_OPTION_CXXFLAGS) \$(ERROR_CXXFLAGS)"
GST_ALL_CFLAGS="-I\$(top_srcdir)/libs -I\$(top_srcdir) -I\$(top_builddir)/libs -I\$(top_builddir) $GLIB_CFLAGS \$(GLIB_EXTRA_CFLAGS) $XML_CFLAGS \$(GST_OPTION_CFLAGS) \$(ERROR_CFLAGS)"

dnl FIXME: check if LTLIBINTL is needed everywhere
dnl I presume it is given that it contains the symbols that _() stuff maps to
GST_ALL_LIBS="$GLIB_LIBS $LTLIBINTL \$(GCOV_LIBS)"

dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_ALL_LDFLAGS="-no-undefined"
if test "x${enable_Bsymbolic}" = "xyes"; then
  GST_ALL_LDFLAGS="$GST_ALL_LDFLAGS -Wl,-Bsymbolic-functions"
fi

AC_SUBST(GST_ALL_CFLAGS)
AC_SUBST(GST_ALL_CXXFLAGS)
AC_SUBST(GST_ALL_LIBS)
AC_SUBST(GST_ALL_LDFLAGS)

dnl GST_LIB_LDFLAGS
dnl linker flags shared by all libraries
dnl LDFLAGS modifier defining exported symbols from built libraries
GST_LIB_LDFLAGS="-export-symbols-regex \^[_]*\(gst_\|Gst\|GST_\).*"
AC_SUBST(GST_LIB_LDFLAGS)

dnl GST_OBJ_*
dnl default vars for all internal objects built on libgstreamer
dnl includes GST_ALL_*
GST_OBJ_CFLAGS="\$(GST_ALL_CFLAGS)"
GST_OBJ_CXXFLAGS="\$(GST_ALL_CXXFLAGS)"
GST_OBJ_LIBS="\$(top_builddir)/gst/libgstreamer-$GST_MAJORMINOR.la \$(GST_ALL_LIBS)"
AC_SUBST(GST_OBJ_CFLAGS)
AC_SUBST(GST_OBJ_CXXFLAGS)
AC_SUBST(GST_OBJ_LIBS)

dnl GST_PLUGIN_LDFLAGS
dnl LDFLAGS for plugins; includes GST_ALL_LDFLAGS
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_desc.*' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS, "$GST_PLUGIN_LDFLAGS")

dnl plugin scanner locations
AS_AC_EXPAND(GST_PLUGIN_SCANNER_INSTALLED,${libexecdir}/gstreamer-$GST_MAJORMINOR/gst-plugin-scanner)
AC_DEFINE_UNQUOTED(GST_PLUGIN_SCANNER_INSTALLED,
    "$GST_PLUGIN_SCANNER_INSTALLED", [location of the installed gst-plugin-scanner])
AC_SUBST(GST_PLUGIN_SCANNER_INSTALLED)

dnl things for our internal libcheck (must be called even if building
dnl libcheck is disabled because it defines conditionals)
AG_GST_CHECK_CHECKS()

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
libs/gst/check/libcheck/Makefile
libs/gst/check/libcheck/check.h
libs/gst/controller/Makefile
libs/gst/dataprotocol/Makefile
libs/gst/helpers/Makefile
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
tests/examples/stepping/Makefile
tests/examples/helloworld/Makefile
tests/examples/launch/Makefile
tests/examples/manual/Makefile
tests/examples/metadata/Makefile
tests/examples/queue/Makefile
tests/examples/streams/Makefile
tests/examples/typefind/Makefile
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
m4/Makefile
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

dnl Create the config.h file for Visual Studio builds
dnl Beware of spaces and /'s in some of the shell variable contents.
sed \
    -e 's/.*config.h.in.*autoheader.*/\/* Autogenerated config.h created for win32 Visual Studio builds *\/\n\n\/* PREFIX -- specifically added for Windows for easier moving *\/\n#define PREFIX "C:\\\\gstreamer"/' \
    -e 's/.* GETTEXT_PACKAGE$/#define GETTEXT_PACKAGE "'$GETTEXT_PACKAGE'"/' \
    -e 's/.* GST_DATADIR$/#define GST_DATADIR PREFIX "\\\\share"/' \
    -e 's/.* GST_LEVEL_DEFAULT$/#define GST_LEVEL_DEFAULT GST_LEVEL_ERROR/' \
    -e 's/.* GST_LICENSE$/#define GST_LICENSE "'$GST_LICENSE'"/' \
    -e 's/.* GST_MAJORMINOR$/#define GST_MAJORMINOR "'$GST_MAJORMINOR'"/' \
    -e "s,.* GST_PACKAGE_NAME$,#define GST_PACKAGE_NAME \"${GST_PACKAGE_NAME}\"," \
    -e 's/.* GST_PACKAGE_ORIGIN$/#define GST_PACKAGE_ORIGIN "Unknown package origin"/' \
    -e "s,.* GST_PACKAGE_RELEASE_DATETIME$,#define GST_PACKAGE_RELEASE_DATETIME \"${GST_PACKAGE_RELEASE_DATETIME}\"," \
    -e 's/.* HAVE_CPU_I386$/#define HAVE_CPU_I386 1/' \
    -e 's/.* HAVE_FGETPOS$/#define HAVE_FGETPOS 1/' \
    -e 's/.* HAVE_FSETPOS$/#define HAVE_FSETPOS 1/' \
    -e 's/.* HAVE_LIBXML2$/#define HAVE_LIBXML2 1/' \
    -e 's/.* HAVE_PROCESS_H$/#define HAVE_PROCESS_H 1/' \
    -e 's/.* HAVE_STDLIB_H$/#define HAVE_STDLIB_H 1/' \
    -e 's/.* HAVE_STRING_H$/#define HAVE_STRING_H 1/' \
    -e 's/.* HAVE_SYS_STAT_H$/#define HAVE_SYS_STAT_H 1/' \
    -e 's/.* HAVE_SYS_TYPES_H$/#define HAVE_SYS_TYPES_H 1/' \
    -e 's/.* HAVE_WIN32$/#define HAVE_WIN32 1/' \
    -e 's/.* HAVE_WINSOCK2_H$/#define HAVE_WINSOCK2_H 1/' \
    -e 's/.* HOST_CPU$/#define HOST_CPU "i686"/' \
    -e 's/.* BUFFER_ALIGNMENT_MALLOC/#define BUFFER_ALIGNMENT_MALLOC 1/' \
    -e 's/.* LIBDIR$/#ifdef _DEBUG\n#  define LIBDIR PREFIX "\\\\debug\\\\lib"\n#else\n#  define LIBDIR PREFIX "\\\\lib"\n#endif/' \
    -e 's/.* LOCALEDIR$/#define LOCALEDIR PREFIX "\\\\share\\\\locale"/' \
    -e 's/.* PACKAGE$/#define PACKAGE "gstreamer"/' \
    -e 's/.* PACKAGE_BUGREPORT$/#define PACKAGE_BUGREPORT "http:\/\/bugzilla.gnome.org\/enter_bug.cgi?product=GStreamer"/' \
    -e 's/.* PACKAGE_NAME$/#define PACKAGE_NAME "'$PACKAGE_NAME'"/' \
    -e "s/.* PACKAGE_STRING$/#define PACKAGE_STRING \"$PACKAGE_STRING\"/" \
    -e 's/.* PACKAGE_TARNAME$/#define PACKAGE_TARNAME "'$PACKAGE_TARNAME'"/' \
    -e 's/.* PACKAGE_VERSION$/#define PACKAGE_VERSION "'$PACKAGE_VERSION'"/' \
    -e 's/.* PLUGINDIR$/#ifdef _DEBUG\n#  define PLUGINDIR PREFIX "\\\\debug\\\\lib\\\\gstreamer-0.10"\n#else\n#  define PLUGINDIR PREFIX "\\\\lib\\\\gstreamer-0.10"\n#endif/' \
    -e 's/.* GST_PLUGIN_SCANNER_INSTALLED$/#define GST_PLUGIN_SCANNER_INSTALLED LIBDIR "\\\\gst-plugin-scanner"/' \
    -e 's/.* VERSION$/#define VERSION "'$VERSION'"/' \
    config.h.in >win32/common/config.h-new

AC_OUTPUT

dnl negate for output
if test "x${GST_DISABLE_GST_DEBUG}" = "xno"; then enable_gst_debug="yes"; fi
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

	Debug Logging              : ${enable_gst_debug}
	Pipeline XML load/save     : ${enable_loadsave}
	Command-line parser        : ${enable_parse}
	Option parsing in gst_init : ${enable_option_parsing}
	Tracing subsystem          : ${enable_trace}
	Allocation tracing         : ${enable_alloc_trace}
	Plugin registry            : ${enable_registry}
	Plugin support	           : ${enable_plugin}
	Network support            : ${enable_net}
	Unit testing support       : ${BUILD_CHECK}

	Debug                      : ${USE_DEBUG}
	Profiling                  : ${USE_PROFILING}

	Building examples          : ${BUILD_EXAMPLES}
	Building test apps         : ${BUILD_TESTS}
	Building tests that fail   : ${BUILD_FAILING_TESTS}
"
