AC_PREREQ([2.69])

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, git and prerelease does Werror too
dnl
AC_INIT([GStreamer],[1.12.2],[http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer],[gstreamer])
AG_GST_INIT

dnl initialize automake (we require GNU make)
AM_INIT_AUTOMAKE([-Wno-portability 1.14 no-dist-gzip dist-xz tar-ustar subdir-objects])

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_GIT="no", GST_GIT="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/gst.c])

dnl define the output header for config
AC_CONFIG_HEADERS([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE([enable])

dnl sets host_* variables
AC_CANONICAL_HOST

dnl use pretty build output by default
AM_SILENT_RULES([yes])

dnl GStreamer versioning, this is mostly informational
GST_VERSION_MAJOR=$PACKAGE_VERSION_MAJOR
GST_VERSION_MINOR=$PACKAGE_VERSION_MINOR
GST_VERSION_MICRO=$PACKAGE_VERSION_MICRO
GST_VERSION_NANO=$PACKAGE_VERSION_NANO
AC_SUBST(GST_VERSION_MAJOR)
AC_SUBST(GST_VERSION_MINOR)
AC_SUBST(GST_VERSION_MICRO)
AC_SUBST(GST_VERSION_NANO)

dnl our libraries and install dirs use GST_API_VERSION in the filename
dnl to allow side-by-side installation of different API versions
GST_API_VERSION=1.0
AC_SUBST(GST_API_VERSION)
AC_DEFINE_UNQUOTED(GST_API_VERSION, "$GST_API_VERSION",
  [GStreamer API Version])

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
dnl
dnl Keep CURRENT as MINOR * 100 + MICRO
dnl Ex : 1.0.0 => 0
dnl      1.0.3 => 3
dnl      1.1.0 => 100
dnl      1.2.5 => 205
dnl      1.10.9 (who knows) => 1009
dnl
dnl sets GST_LT_LDFLAGS
AS_LIBTOOL(GST, 1202, 0, 1202)

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal (keep in sync with Makefile.am)
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl set up gettext
dnl the version check needs to stay here because autopoint greps for it
AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])
AG_GST_GETTEXT([gstreamer-$GST_API_VERSION])

dnl *** check for arguments to configure ***

AG_GST_ARG_DISABLE_FATAL_WARNINGS
AG_GST_ARG_ENABLE_EXTRA_CHECKS

dnl subsystems - can influence other decisions so needs to be high up
dnl we need to AM_CONDITIONAL them here for automake 1.6.x compatibility
AG_GST_CHECK_SUBSYSTEM_DISABLE(GST_DEBUG,[debugging subsystem])
AM_CONDITIONAL(GST_DISABLE_GST_DEBUG, test "x$GST_DISABLE_GST_DEBUG" = "xyes")
AG_GST_CHECK_SUBSYSTEM_DISABLE(GST_TRACER_HOOKS,[tracing subsystem hooks])
AM_CONDITIONAL(GST_DISABLE_GST_TRACER_HOOKS, test "x$GST_DISABLE_GST_TRACER_HOOKS" = "xyes")
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

# We only use this when building with MSVC, which is only done with the
# alternate Meson build system files
GSTCONFIG_BUILT_WITH_MSVC=0
AC_SUBST(GSTCONFIG_BUILT_WITH_MSVC)

dnl check for bash completion
AC_ARG_WITH([bash-completion-dir],
    AS_HELP_STRING([--with-bash-completion-dir[=PATH]],
        [Install the bash auto-completion script in this directory. @<:@default=yes@:>@]),
    [],
    [with_bash_completion_dir=yes])

if test "x$with_bash_completion_dir" = "xyes"
then
    extra_args=

    if test "x$prefix" != "xNONE"
    then
        extra_args="--define-variable=prefix=\"$prefix\""
    fi

    PKG_CHECK_MODULES([BASH_COMPLETION], [bash-completion >= 2.0],
        [
          BASH_COMPLETION_DIR="`eval pkg-config $extra_args --variable=completionsdir bash-completion`"
          BASH_HELPERS_DIR="`eval pkg-config $extra_args --variable=helpersdir bash-completion`"
        ],
        [
          BASH_COMPLETION_DIR="$datadir/bash-completion/completions"
          BASH_HELPERS_DIR="$datadir/bash-completion/helpers"
        ])
else
    BASH_COMPLETION_DIR="$with_bash_completion_dir/completions"
    BASH_HELPERS_DIR="$with_bash_completion_dir/helpers"
fi

AC_SUBST([BASH_COMPLETION_DIR])
AC_SUBST([BASH_HELPERS_DIR])
AM_CONDITIONAL([ENABLE_BASH_COMPLETION],[test "x$with_bash_completion_dir" != "xno"])

dnl build static plugins or not
AC_MSG_CHECKING([whether to build static plugins or not])
AC_ARG_ENABLE(
  static-plugins,
  AC_HELP_STRING(
    [--enable-static-plugins],
    [build static plugins @<:@default=no@:>@]),
  [AS_CASE(
    [$enableval], [no], [], [yes], [],
    [AC_MSG_ERROR([bad value "$enableval" for --enable-static-plugins])])],
  [enable_static_plugins=no])
AC_MSG_RESULT([$enable_static_plugins])
if test "x$enable_static_plugins" = xyes; then
  AC_DEFINE(GST_PLUGIN_BUILD_STATIC, 1,
    [Define if static plugins should be built])
  GST_PLUGIN_LIBTOOLFLAGS=""
else
  GST_PLUGIN_LIBTOOLFLAGS="--tag=disable-static"
fi
AC_SUBST(GST_PLUGIN_LIBTOOLFLAGS)
AM_CONDITIONAL(GST_PLUGIN_BUILD_STATIC, test "x$enable_static_plugins" = "xyes")

dnl If only building static libraries, define GST_STATIC_COMPILATION. This is
dnl needed only on Windows, but it doesn't hurt to have it everywhere.
if test x$enable_static = xyes -a x$enable_shared = xno; then
  GST_OBJ_STATIC_CFLAGS="-DGST_STATIC_COMPILATION"
fi

dnl building of tests
AC_ARG_ENABLE(tests,
  AS_HELP_STRING([--disable-tests],[disable building test apps]),
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
  AS_HELP_STRING([--disable-failing-tests],[disable building tests known to fail]),
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

dnl building of benchmarks
AC_ARG_ENABLE(benchmarks,
  AS_HELP_STRING([--disable-benchmarks],[disable building benchmarks apps]),
  [
    case "${enableval}" in
      yes) BUILD_BENCHMARKS=yes ;;
      no)  BUILD_BENCHMARKS=no ;;
      *)   AC_MSG_ERROR(bad value ${enableval} for --disable-benchmarks) ;;
    esac
  ],
[BUILD_BENCHMARKS=yes]) dnl Default value
AM_CONDITIONAL(BUILD_BENCHMARKS, test "x$BUILD_BENCHMARKS" = "xyes")

dnl building of tools
AC_ARG_ENABLE(tools,
  AS_HELP_STRING([--disable-tools],[disable building tools]),
  [
    case "${enableval}" in
      yes) BUILD_TOOLS=yes ;;
      no)  BUILD_TOOLS=no ;;
      *)   AC_MSG_ERROR(bad value ${enableval} for --disable-tools) ;;
    esac
  ],
[BUILD_TOOLS=yes]) dnl Default value
AM_CONDITIONAL(BUILD_TOOLS, test "x$BUILD_TOOLS" = "xyes")

dnl poison destroyed objects
AC_ARG_ENABLE(poisoning,
  AS_HELP_STRING([--enable-poisoning],[enable poisoning of deallocated objects]),
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

dnl PTP support parts
AC_MSG_CHECKING([whether PTP support can be enabled])
case "$host_os" in
  *android*)
    dnl Can't run on Android because of permissions
    HAVE_PTP=no
    ;;
  mingw*|pw32*|cygwin*)
    dnl Not ported to Windows yet
    HAVE_PTP=no
    ;;
  darwin*)
    dnl Can't run on iOS because of permissions
    AC_CHECK_HEADER(MobileCoreServices/MobileCoreServices.h, HAVE_PTP="no", HAVE_PTP="yes", [-])
    ;;
  linux*|darwin*|solaris*|netbsd*|freebsd*|openbsd*|kfreebsd*|dragonfly*|gnu*)
    HAVE_PTP=yes
    ;;
  *)
    HAVE_PTP=no
    ;;
esac
AC_MSG_RESULT([$HAVE_PTP])

dnl user/group to change to in gst-ptp-helper
AC_ARG_WITH([ptp-helper-setuid-user],
  AS_HELP_STRING([--with-ptp-helper-setuid-user],[User to switch to when installing gst-ptp-helper setuid root]),
  [
    if test "x$withval" != "x"
    then
      AC_DEFINE_UNQUOTED(HAVE_PTP_HELPER_SETUID_USER, "$withval", [PTP helper setuid user])
    fi
  ], []
)

dnl group/group to change to in gst-ptp-helper
AC_ARG_WITH([ptp-helper-setuid-group],
  AS_HELP_STRING([--with-ptp-helper-setuid-group],[Group to switch to when installing gst-ptp-helper setuid root]),
  [
    if test "x$withval" != "x"
    then
      AC_DEFINE_UNQUOTED(HAVE_PTP_HELPER_SETUID_GROUP, "$withval", [PTP helper setuid group])
    fi
  ], []
)

AC_ARG_WITH(
  ptp-helper-permissions,
  AC_HELP_STRING(
    [--with-ptp-helper-permissions],
    [how to gain PTP permissions (none, setuid-root, capabilities, auto)]),
    [],
    [with_ptp_helper_permissions=auto])

gst_ptp_have_cap=no
AG_GST_CHECK_LIBHEADER(CAP, cap,
                       cap_init, ,
                       sys/capability.h,
                       CAP_LIBS="-lcap"
                       AC_SUBST(CAP_LIBS)
                       gst_ptp_have_cap=yes)

AC_PATH_PROG([SETCAP], [setcap], [no], [$PATH:/usr/bin:/bin:/usr/sbin:/sbin])

if test "x$HAVE_PTP" = "xyes"; then
AC_DEFINE(HAVE_PTP, 1, [PTP support available])

AC_MSG_CHECKING([for SIOCGIFCONF, SIOCGIFFLAGS and SIOCGIFHWADDR])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
    [[
        #include <sys/ioctl.h>
        #include <net/if.h>
    ]],
    [[
        struct ifreq ifr;
        struct ifconf ifc;
        ioctl(0, SIOCGIFCONF, &ifc);
        ioctl(0, SIOCGIFFLAGS, &ifr);
        ioctl(0, SIOCGIFHWADDR, &ifr);
        int dummy = ifr.ifr_hwaddr.sa_data[0];
    ]])], [
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_SIOCGIFCONF_SIOCGIFFLAGS_SIOCGIFHWADDR, 1, [SIOCGIFCONF, SIOCGIFFLAGS and SIOCGIFHWADDR is available])
    ], [
        AC_MSG_RESULT(no)
])

AC_MSG_CHECKING([for getifaddrs() and AF_LINK])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM(
    [[
        #include <ifaddrs.h>
        #include <net/if.h>
        #include <net/if_dl.h>
    ]],
    [[
        struct ifaddrs *ifaddr;
        getifaddrs(&ifaddr);
        int dummy = (ifaddr->ifa_flags & IFF_LOOPBACK) && ifaddr->ifa_addr->sa_family != AF_LINK;
    ]])], [
        AC_MSG_RESULT(yes)
        AC_DEFINE(HAVE_GETIFADDRS_AF_LINK, 1, [getifaddrs() and AF_LINK is available])
    ], [
        AC_MSG_RESULT(no)
    ])

AC_MSG_CHECKING([how to install gst-ptp-helper])
if test "x$with_ptp_helper_permissions" = "xauto"; then
    if test "x$gst_ptp_have_cap" = "xyes" -a "x$SETCAP" != "xno"; then
        with_ptp_helper_permissions="capabilities"
    else
        with_ptp_helper_permissions="setuid-root"
    fi
fi
AC_MSG_RESULT([$with_ptp_helper_permissions])

case "$with_ptp_helper_permissions" in
  none)
    ;;
  setuid-root)
     AC_DEFINE(HAVE_PTP_HELPER_SETUID, 1,
        [Use setuid-root for permissions in PTP helper])
    ;;
  capabilities)
     AC_DEFINE(HAVE_PTP_HELPER_CAPABILITIES, 1,
        [Use capabilities for permissions in PTP helper])
    ;;
  *)
    AC_MSG_ERROR(Invalid parameter [$with_ptp_helper_permissions])
    ;;
esac

fi

AM_CONDITIONAL(HAVE_PTP, test "x$HAVE_PTP" = "xyes")
AM_CONDITIONAL(HAVE_PTP_HELPER_SETUID, test "x$with_ptp_helper_permissions" = "xsetuid-root")
AM_CONDITIONAL(HAVE_PTP_HELPER_CAPABILITIES, test "x$with_ptp_helper_permissions" = "xcapabilities")

dnl *** checks for platform ***

dnl * hardware/architecture *

dnl common/m4/gst-arch.m4
dnl check CPU type
AG_GST_ARCH

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
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[ class Foo { int bar; };]], [[]])],[working_cxx=yes],[working_cxx=no])
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
GOBJECT_INTROSPECTION_CHECK([1.31.1])

dnl check for documentation tools
GTK_DOC_CHECK([1.12])
AG_GST_PLUGIN_DOCS([1.12])

dnl *** checks for libraries ***

dnl check for libm, for sin()
LT_LIB_M
AC_SUBST(LIBM)

dnl *** checks for header files ***

dnl check if we have ANSI C header files
AC_HEADER_STDC

dnl Check for ucontext.h
AC_CHECK_HEADERS([ucontext.h], [], [], [AC_INCLUDES_DEFAULT])

dnl Check for sys/socket.h
AC_CHECK_HEADERS([sys/socket.h], [HAVE_SYS_SOCKET_H=yes], [HAVE_SYS_SOCKET_H=no], [AC_INCLUDES_DEFAULT])
AM_CONDITIONAL(HAVE_SYS_SOCKET_H, test "x$HAVE_SYS_SOCKET_H" = "xyes")

dnl check for sys/times.h for tests/examples/adapter/
AC_CHECK_HEADERS([sys/times.h], [HAVE_SYS_TIMES_H=yes], [HAVE_SYS_TIME_H=no], [AC_INCLUDES_DEFAULT])
AC_CHECK_HEADERS([unistd.h], [HAVE_UNISTD_H=yes], [HAVE_UNISTD_H=no], [AC_INCLUDES_DEFAULT])
AM_CONDITIONAL(HAVE_SYS_TIMES_H_AND_UNISTD_H, test "x$HAVE_SYS_TIMES_H" = "xyes" -a "x$HAVE_UNISTD_H" = "xyes")

dnl Check for process.h for getpid() on win32
AC_CHECK_HEADERS([process.h], [], [], [AC_INCLUDES_DEFAULT])

dnl Check for sys/utsname.h for uname
AC_CHECK_HEADERS([sys/utsname.h], [], [], [AC_INCLUDES_DEFAULT])

dnl Check for stdio_ext.f for __fbufsize
AC_CHECK_HEADERS([stdio_ext.h], [], [], [AC_INCLUDES_DEFAULT])

dnl check for pthreads
dnl without arguments AX_PTHREAD() will do AC_DEFINE(HAVE_PTHREAD)
dnl which later checks use in their test code
AX_PTHREAD()
AM_CONDITIONAL(HAVE_PTHREAD, test "x$ax_pthread_ok" = "xyes")

dnl check for sys/prctl for setting thread name on Linux
AC_CHECK_HEADERS([sys/prctl.h], [], [], [AC_INCLUDES_DEFAULT])

dnl check for pthread_setname_np(const char*)
dnl which is present on OS X 10.6, iOS 3.2 and above
AC_MSG_CHECKING(for pthread_setname_np(const char*))
AC_LINK_IFELSE(
    [AC_LANG_PROGRAM(
        [#include <pthread.h>],
        [pthread_setname_np("example")])],
    [AC_MSG_RESULT(yes)
     AC_DEFINE(HAVE_PTHREAD_SETNAME_NP_WITHOUT_TID,1,
        [Have function pthread_setname_np(const char*)])],
    [AC_MSG_RESULT(no)])

dnl check for sys/uio.h for writev()
AC_CHECK_HEADERS([sys/uio.h], [], [], [AC_INCLUDES_DEFAULT])

dnl Check for valgrind.h
dnl separate from HAVE_VALGRIND because you can have the program, but not
dnl the dev package
AC_CHECK_HEADERS([valgrind/valgrind.h], [], [], [AC_INCLUDES_DEFAULT])

dnl used in gst/gstpoll.c
AC_CHECK_HEADERS([winsock2.h], [HAVE_WINSOCK2_H=yes], [HAVE_WINSOCK2_H=no], [AC_INCLUDES_DEFAULT])
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
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[ ]], [[
      static __uint128_t v1 = 100;
      static __uint128_t v2 = 10;
      static __uint128_t u;
      u = v1 / v2;
    ]])],[
      gst_cv_uint128_t=yes
    ],[
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

AC_CHECK_FUNCS([strcasestr])

AC_CHECK_FUNCS([gmtime_r])
AC_CHECK_FUNCS([localtime_r])
AC_CHECK_FUNCS([sigaction])
AC_CHECK_FUNCS([getrusage])
AM_CONDITIONAL(HAVE_GETRUSAGE, test "x$ac_cv_func_getrusage" = "xyes")
AC_CHECK_HEADERS([sys/resource.h])

dnl check for fseeko()
AC_FUNC_FSEEKO
dnl check for ftello()
AC_CHECK_FUNCS([ftello])

AC_CHECK_FUNCS([fgetpos])
AC_CHECK_FUNCS([fsetpos])

dnl check for poll(), ppoll() and pselect()
AC_CHECK_HEADERS([sys/poll.h], [], [], [AC_INCLUDES_DEFAULT])
AC_CHECK_HEADERS([poll.h], [], [], [AC_INCLUDES_DEFAULT])
AC_CHECK_FUNCS([poll])
AC_CHECK_FUNCS([ppoll])
AC_CHECK_FUNCS([pselect])

dnl check for socketpair()
AC_CHECK_FUNC(socketpair, [], [
  AC_CHECK_LIB(socket, socketpair, [
    SOCKET_LIBS="-lsocket"
    AC_SUBST(SOCKET_LIBS)
  ])
])

dnl ****************************************
dnl *** GLib POLL* compatibility defines ***
dnl ****************************************

AC_MSG_CHECKING([for broken poll])
AC_RUN_IFELSE([AC_LANG_SOURCE([[
	#include <stdlib.h>
	#include <fcntl.h>
	#ifdef HAVE_SYS_POLL_H
	#include <sys/poll.h>
	#endif
	#ifdef HAVE_POLL_H
	#include <poll.h>
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

dnl check for getpagesize()
AC_CHECK_FUNCS([getpagesize])

dnl Check for POSIX timers
CLOCK_GETTIME_FOUND="no"
AC_CHECK_FUNC(clock_gettime, [CLOCK_GETTIME_FOUND="yes"], [
  AC_CHECK_LIB(rt, clock_gettime, [
    CLOCK_GETTIME_FOUND="yes"
    LIBS="$LIBS -lrt"
  ], [
    AC_CHECK_LIB(pthread, clock_gettime, [
      CLOCK_GETTIME_FOUND="yes"
      LIBS="$LIBS -lpthread"
    ])
  ])
])

# With XCode 8, clock_gettime will be incorrectly detected as being available
# regardless of what version of OS X you target because the symbol is available
# in the .tbd file as a weak symbol.
# See: https://bugzilla.gnome.org/show_bug.cgi?id=772451 
#
# We cannot simply do AC_CHECK_FUNCS with -Wl,-no_weak_imports because the
# autoconf check does its own prototype declaration that doesn't trigger that
# compiler flag.
#
# It's only starting from macOS 10.12 and iOS 10.0 that clock_gettime is
# actually available, so we can unconditionally disable it for older versions.
case "$host_os" in
  darwin*)
    AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
#include <AvailabilityMacros.h>
#include <TargetConditionals.h>
#if defined(TARGET_OS_MAC)
#  if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_12
#    error "Not compiling for OS X 10.12 or later"
#  endif
#else
#  if __IPHONE_OS_VERSION_MIN_REQUIRED < __IPHONE_10_0
#    error "Not compiling for iOS 10.0 or later"
#  endif
#endif
    ]])], [], [
    if test "$CLOCK_GETTIME_FOUND" = "yes"; then
      AC_MSG_NOTICE([Disabling incorrectly detected clock_gettime on OS X])
    fi
    CLOCK_GETTIME_FOUND="no"
    ])
    ;;
esac

if test "$CLOCK_GETTIME_FOUND" = "yes"; then
  AC_DEFINE(HAVE_CLOCK_GETTIME, 1, [Have clock_gettime])
fi

AC_CACHE_CHECK(for posix timers, gst_cv_posix_timers,
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <time.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
    ]], [[
#if !defined(_POSIX_TIMERS) || _POSIX_TIMERS < 0 || !defined(CLOCK_REALTIME)
#error Either _POSIX_TIMERS or CLOCK_REALTIME not defined
#endif
    ]])],[
      gst_cv_posix_timers=yes
    ],[
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
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
#include <time.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
    ]], [[
#if !defined(_POSIX_MONOTONIC_CLOCK) || _POSIX_MONOTONIC_CLOCK < 0 || !defined(CLOCK_MONOTONIC)
#error Either _POSIX_MONOTONIC_CLOCK or CLOCK_MONOTONIC not defined
#endif
    ]])],[
      gst_cv_monotonic_clock=yes
    ],[
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

dnl test if we have dladdr(); we use it for debugging; see gst/gstinfo.c
save_cflags="$CFLAGS"
CFLAGS="$CFLAGS -D_GNU_SOURCE"
AC_CHECK_LIB(dl, dladdr,
   AC_DEFINE(HAVE_DLADDR, 1, [Defined if we have dladdr ()])
   LIBS="$LIBS -ldl")
CFLAGS="$save_cflags"

dnl Check printf stuff
if test "x${GST_DISABLE_GST_DEBUG}" != "xyes"; then
  AC_TYPE_LONG_LONG_INT
  AC_TYPE_UNSIGNED_LONG_LONG_INT

  if test x$ac_cv_type_long_long_int$ac_cv_type_unsigned_long_long_int = xyesyes; then
    AC_DEFINE([HAVE_LONG_LONG], [1], [Define to 1 if the system has the type long long])
  fi

  dnl /usr/share/aclocal/inttypes_h.m4 - ships with gettext apparently
  gl_AC_HEADER_INTTYPES_H

  dnl /usr/share/aclocal/stdint_h.m4 - ships with gettext apparently
  gl_AC_HEADER_STDINT_H

  AC_CHECK_TYPES(ptrdiff_t)

  AC_TYPE_INTMAX_T
  AC_TYPE_SIZE_T
fi

dnl *** checks for dependency libraries ***

dnl GLib
GLIB_REQ=2.40.0
AG_GST_GLIB_CHECK([$GLIB_REQ])

dnl Check for documentation xrefs
GLIB_PREFIX="`$PKG_CONFIG --variable=prefix glib-2.0`"
AC_SUBST(GLIB_PREFIX)

dnl GTK is optional and only used in examples
HAVE_GTK=no
GTK_REQ=3.10
if test "x$BUILD_EXAMPLES" = "xyes"; then
  PKG_CHECK_MODULES(GTK, gtk+-3.0 >= $GTK_REQ, HAVE_GTK=yes, HAVE_GTK=no)
  AC_SUBST(GTK_LIBS)
  AC_SUBST(GTK_CFLAGS)
fi
AM_CONDITIONAL(HAVE_GTK, test "x$HAVE_GTK" = "xyes")

dnl libunwind is optionally used by the leaks tracer
PKG_CHECK_MODULES(UNWIND, libunwind, HAVE_UNWIND=yes, HAVE_UNWIND=no)
if test "x$HAVE_UNWIND" = "xyes"; then
  AC_DEFINE(HAVE_UNWIND, 1, [libunwind available])
fi

dnl libdw is optionally used to add source lines and numbers to backtraces
PKG_CHECK_MODULES(DW, libdw, HAVE_DW=yes, HAVE_DW=no)
if test "x$HAVE_DW" = "xyes"; then
  AC_DEFINE(HAVE_DW, 1, [libdw available])
fi

dnl Check for backtrace() from libc
AC_CHECK_FUNC(backtrace, [
  AC_CHECK_HEADERS([execinfo.h], [
    AC_DEFINE(HAVE_BACKTRACE,1,[Have backtrace])
  ], [], [])
])

dnl building of unit test libraries
AC_ARG_ENABLE(check,
  AS_HELP_STRING([--disable-check],[disable building unit test libraries]),
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

dnl configure the desired memory alignment
AC_ARG_WITH([memory-alignment],
  AS_HELP_STRING([--with-memory-alignment],[8,N,malloc,pagesize (default is 32)]),
  [
    if test "x$withval" = "xyes"
    then
      AC_DEFINE(MEMORY_ALIGNMENT, 32, [Memory alignment to use])
    else
      case "${withval}" in
        malloc) AC_DEFINE(MEMORY_ALIGNMENT_MALLOC, 1, [Memory alignment by malloc default]) ;;
        pagesize)  AC_DEFINE(MEMORY_ALIGNMENT_PAGESIZE, 1, [Memory alignment by pagesize]) ;;
        *)   AC_DEFINE_UNQUOTED(MEMORY_ALIGNMENT, ${withval}, [Memory alignment to use]) ;;
      esac
    fi
  ], [
    AC_DEFINE(MEMORY_ALIGNMENT_MALLOC, 1, [Memory alignment by malloc default])
  ]
)

dnl Check for -Bsymbolic-functions linker flag used to avoid
dnl intra-library PLT jumps, if available.
AC_ARG_ENABLE(Bsymbolic,
              [AS_HELP_STRING([--disable-Bsymbolic],[avoid linking with -Bsymbolic])],,
              [SAVED_LDFLAGS="${LDFLAGS}" SAVED_LIBS="${LIBS}"
               AC_MSG_CHECKING([for -Bsymbolic-functions linker flag])
               LDFLAGS=-Wl,-Bsymbolic-functions
               LIBS=
               AC_TRY_LINK([], [return 0],
                           AC_MSG_RESULT(yes)
                           enable_Bsymbolic=yes,
                           AC_MSG_RESULT(no)
                           enable_Bsymbolic=no)
               LDFLAGS="${SAVED_LDFLAGS}" LIBS="${SAVED_LIBS}"])


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

dnl make sure it doesn't complain about unused variables if debugging is disabled
NO_WARNINGS=""
if test "x${GST_DISABLE_GST_DEBUG}" = "xyes"; then
  NO_WARNINGS="-Wno-unused"
fi

dnl define an ERROR_CFLAGS Makefile variable
AG_GST_SET_ERROR_CFLAGS($FATAL_WARNINGS, [-Wmissing-declarations -Wmissing-prototypes -Wredundant-decls -Wundef -Wwrite-strings -Wformat-nonliteral -Wformat-security -Wold-style-definition -Winit-self -Wmissing-include-dirs -Waddress -Waggregate-return -Wno-multichar -Wnested-externs $NO_WARNINGS])

dnl special warning flags for gst/printf
AS_COMPILER_FLAG([-Wno-format-nonliteral], [PRINTF_CFLAGS="-Wno-format-nonliteral"])
AC_SUBST(PRINTF_CFLAGS)

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
GST_OPTION_CFLAGS="$GST_OPTION_CFLAGS"
GST_OPTION_CXXFLAGS="$GST_OPTION_CXXFLAGS"
AC_SUBST(GST_OPTION_CFLAGS)
AC_SUBST(GST_OPTION_CXXFLAGS)

dnl GST_ALL_*
dnl vars common to for all internal objects (core libs, elements, applications)
dnl CFLAGS:
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
GST_LIB_LDFLAGS="-export-symbols-regex \^[_]?\(gst_\|Gst\|GST_\).*"
AC_SUBST(GST_LIB_LDFLAGS)

dnl GST_OBJ_*
dnl default vars for all internal objects built on libgstreamer
dnl includes GST_ALL_*
GST_OBJ_CFLAGS="\$(GST_ALL_CFLAGS) $GST_OBJ_STATIC_CFLAGS"
GST_OBJ_CXXFLAGS="\$(GST_ALL_CXXFLAGS) $GST_OBJ_STATIC_CFLAGS"
GST_OBJ_LIBS="\$(top_builddir)/gst/libgstreamer-$GST_API_VERSION.la \$(GST_ALL_LIBS)"
AC_SUBST(GST_OBJ_CFLAGS)
AC_SUBST(GST_OBJ_CXXFLAGS)
AC_SUBST(GST_OBJ_LIBS)

dnl GST_PLUGIN_LDFLAGS
dnl LDFLAGS for plugins; includes GST_ALL_LDFLAGS
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_.*' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS, "$GST_PLUGIN_LDFLAGS")

dnl plugin scanner locations
AS_AC_EXPAND(GST_PLUGIN_SCANNER_INSTALLED,${libexecdir}/gstreamer-$GST_API_VERSION/gst-plugin-scanner)
AC_DEFINE_UNQUOTED(GST_PLUGIN_SCANNER_INSTALLED,
    "$GST_PLUGIN_SCANNER_INSTALLED", [location of the installed gst-plugin-scanner])
AC_SUBST(GST_PLUGIN_SCANNER_INSTALLED)

dnl completion helper locations
AS_AC_EXPAND(GST_COMPLETION_HELPER_INSTALLED,${libexecdir}/gstreamer-$GST_API_VERSION/gst-completion-helper)
AC_DEFINE_UNQUOTED(GST_COMPLETION_HELPER_INSTALLED,
    "$GST_COMPLETION_HELPER_INSTALLED", [location of the installed gst-completion-helper])
AC_SUBST(GST_COMPLETION_HELPER_INSTALLED)

dnl ptp helper locations
AS_AC_EXPAND(GST_PTP_HELPER_INSTALLED,${libexecdir}/gstreamer-$GST_API_VERSION/gst-ptp-helper)
AC_DEFINE_UNQUOTED(GST_PTP_HELPER_INSTALLED,
    "$GST_PTP_HELPER_INSTALLED", [location of the installed gst-ptp-helper])
AC_SUBST(GST_PTP_HELPER_INSTALLED)

dnl things for our internal libcheck (must be called even if building
dnl libcheck is disabled because it defines conditionals)
AG_GST_CHECK_CHECKS

AC_CONFIG_FILES(
Makefile
data/Makefile
data/bash-completion/helpers/gst
gst/Makefile
gst/gstconfig.h
gst/gstversion.h
gst/parse/Makefile
gst/printf/Makefile
libs/Makefile
libs/gst/Makefile
libs/gst/base/Makefile
libs/gst/check/Makefile
libs/gst/check/libcheck/Makefile
libs/gst/check/internal-check.h:libs/gst/check/libcheck/check.h.in
libs/gst/controller/Makefile
libs/gst/helpers/Makefile
libs/gst/net/Makefile
plugins/Makefile
plugins/elements/Makefile
plugins/tracers/Makefile
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
tests/examples/memory/Makefile
tests/examples/netclock/Makefile
tests/examples/ptp/Makefile
tests/examples/streamiddemux/Makefile
tests/examples/streams/Makefile
tools/Makefile
common/Makefile
common/m4/Makefile
docs/Makefile
docs/gst/Makefile
docs/gst/gstreamer.types
docs/libs/Makefile
docs/plugins/Makefile
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
pkgconfig/gstreamer-net.pc
pkgconfig/gstreamer-net-uninstalled.pc
)

AC_OUTPUT

dnl negate for output
if test "x${GST_DISABLE_GST_DEBUG}" = "xno"; then enable_gst_debug="yes"; fi
if test "x${GST_DISABLE_GST_TRACER_HOOKS}" = "xno"; then enable_gst_tracer_hooks="yes"; fi
if test "x${GST_DISABLE_PARSE}" = "xno"; then enable_parse="yes"; fi
if test "x${GST_DISABLE_OPTION_PARSING}" = "xno"; then enable_option_parsing="yes"; fi
if test "x${GST_DISABLE_PLUGIN}" = "xno"; then enable_plugin="yes"; fi
if test "x${GST_DISABLE_REGISTRY}" = "xno"; then enable_registry="yes"; fi

echo "

Configuration
	Version                    : ${VERSION}
	Source code location       : ${srcdir}
	Prefix                     : ${prefix}
	Compiler                   : ${CC}
	Package name               : ${GST_PACKAGE_NAME}
	Package origin             : ${GST_PACKAGE_ORIGIN}

	API Documentation          : ${enable_gtk_doc}

	Debug logging              : ${enable_gst_debug}
	Tracing subsystem hooks    : ${enable_gst_tracer_hooks}
	Command-line parser        : ${enable_parse}
	Option parsing in gst_init : ${enable_option_parsing}
	Plugin registry            : ${enable_registry}
	Plugin support	           : ${enable_plugin}
	Static plugins             : ${enable_static_plugins}
	Unit testing support       : ${BUILD_CHECK}
	PTP clock support          : ${HAVE_PTP}
	libunwind support          : ${HAVE_UNWIND}
	libdw support              : ${HAVE_DW}

	Debug                      : ${USE_DEBUG}
	Profiling                  : ${USE_PROFILING}

	Building benchmarks        : ${BUILD_BENCHMARKS}
	Building examples          : ${BUILD_EXAMPLES}
	Building test apps         : ${BUILD_TESTS}
	Building tests that fail   : ${BUILD_FAILING_TESTS}
	Building tools             : ${BUILD_TOOLS}
"
