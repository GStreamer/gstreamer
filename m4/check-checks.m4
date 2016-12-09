dnl Check for things that check needs/wants and that we don't check for already
dnl AM_GST_CHECK_CHECKS()

AC_DEFUN([AG_GST_CHECK_CHECKS],
[
AC_MSG_NOTICE([Running check unit test framework checks now...])

CHECK_MAJOR_VERSION=0
CHECK_MINOR_VERSION=10
CHECK_MICRO_VERSION=0
CHECK_VERSION=$CHECK_MAJOR_VERSION.$CHECK_MINOR_VERSION.$CHECK_MICRO_VERSION

AC_SUBST(CHECK_MAJOR_VERSION)
AC_SUBST(CHECK_MINOR_VERSION)
AC_SUBST(CHECK_MICRO_VERSION)
AC_SUBST(CHECK_VERSION)

dnl Checks for header files and declarations
AC_CHECK_HEADERS([unistd.h sys/wait.h sys/time.h], [], [], [AC_INCLUDES_DEFAULT])

dnl Check for localtime_r()
AC_CHECK_FUNCS([localtime_r])
AM_CONDITIONAL(HAVE_LOCALTIME_R, test "x$ac_cv_func_localtime_r" = "xyes")

dnl Check for gettimeofday()
AC_CHECK_FUNCS([gettimeofday])
AM_CONDITIONAL(HAVE_GETTIMEOFDAY, test "x$ac_cv_func_gettimeofday" = "xyes")

dnl Check for getpid() and _getpid()
AC_CHECK_FUNCS([getpid _getpid])

dnl Check for strdup() and _strdup()
AC_CHECK_DECLS([strdup])
AC_CHECK_FUNCS([_strdup])
AM_CONDITIONAL(HAVE_STRDUP, test "x$ac_cv_have_decl_strdup" = "xyes" -o "x$ac_cv_func__strdup" = "xyes")

dnl Check for getline()
AC_CHECK_FUNCS([getline])
AM_CONDITIONAL(HAVE_GETLINE, test "x$ac_cv_func_getline" = "xyes")

dnl Check for mkstemp
AC_CHECK_FUNCS([mkstemp])

dnl Check for fork
AC_CHECK_FUNCS([fork], HAVE_FORK=1, HAVE_FORK=0)
AC_SUBST(HAVE_FORK)

dnl Check for alarm, localtime_r and strsignal
dnl First check for time.h as it might be used by localtime_r
AC_CHECK_HEADERS([time.h])
AC_CHECK_DECLS([alarm, localtime_r, strsignal], [], [], [
    AC_INCLUDES_DEFAULT
#if HAVE_TIME_H
#include <time.h>
#endif /* HAVE_TIME_H */
])
AC_CHECK_FUNCS([alarm setitimer strsignal])
AM_CONDITIONAL(HAVE_ALARM, test "x$ac_cv_func_alarm" = "xyes")
AM_CONDITIONAL(HAVE_LOCALTIME_R, test "x$ac_cv_func_localtime_r" = "xyes")
AM_CONDITIONAL(HAVE_STRSIGNAL, test "x$ac_cv_func_strsignal" = "xyes")

dnl Check if struct timespec/itimerspec are defined in time.h. If not, we need
dnl to define it in libs/gst/check/libcheck/libcompat.h. Note the optional
dnl inclusion of pthread.h. On MinGW(-w64), the pthread.h file contains the
dnl timespec/itimerspec definitions.
AC_CHECK_MEMBERS([struct timespec.tv_sec, struct timespec.tv_nsec], [],
  [AC_DEFINE_UNQUOTED(STRUCT_TIMESPEC_DEFINITION_MISSING, 1,
    [Need to define the timespec structure])], [
#include <time.h>
#if HAVE_PTHREAD
#include <pthread.h>
#endif /* HAVE_PTHREAD */
])
AC_CHECK_MEMBERS([struct itimerspec.it_interval, struct itimerspec.it_value],
  [], [AC_DEFINE_UNQUOTED(STRUCT_ITIMERSPEC_DEFINITION_MISSING, 1,
    [Need to define the itimerspec structure])], [
#include <time.h>
#if HAVE_PTHREAD
#include <pthread.h>
#endif /* HAVE_PTHREAD */
])

dnl Check if types timer_t/clockid_t are defined. If not, we need to define it
dnl in libs/gst/check/libcheck/libcompat/libcompat.h. Note the optional
dnl inclusion of pthread.h. On MinGW(-w64), the pthread.h file contains the
dnl timer_t/clockid_t definitions.
AC_CHECK_TYPE(timer_t, [], [
    AC_DEFINE([timer_t], [int], [timer_t])
  ], [
    AC_INCLUDES_DEFAULT
#if HAVE_PTHREAD
#include <pthread.h>
#endif /* HAVE_PTHREAD */
])
AC_CHECK_TYPE(clockid_t, [], [
    AC_DEFINE([clockid_t], [int], [clockid_t])
  ], [
    AC_INCLUDES_DEFAULT
#if HAVE_PTHREAD
#include <pthread.h>
#endif /* HAVE_PTHREAD */
])

dnl Check for POSIX timer functions in librt
AC_CHECK_LIB([rt], [timer_create, timer_settime, timer_delete])
AM_CONDITIONAL(HAVE_TIMER_CREATE_SETTIME_DELETE, test "x$ac_cv_lib_rt_timer_create__timer_settime__timer_delete" = "xyes")

dnl Allow for checking HAVE_CLOCK_GETTIME in automake files
AM_CONDITIONAL(HAVE_CLOCK_GETTIME, test "x$CLOCK_GETTIME_FOUND" = "xyes")

dnl Create _stdint.h in the top-level directory
AX_CREATE_STDINT_H

dnl Disable subunit support for the time being
enable_subunit=false

if test xfalse = x"$enable_subunit"; then
ENABLE_SUBUNIT="0"
else
ENABLE_SUBUNIT="1"
fi
AC_SUBST(ENABLE_SUBUNIT)
AC_DEFINE_UNQUOTED(ENABLE_SUBUNIT, $ENABLE_SUBUNIT, [Subunit protocol result output])

AM_CONDITIONAL(SUBUNIT, test x"$enable_subunit" != "xfalse")

])
