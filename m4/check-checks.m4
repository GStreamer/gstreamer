dnl Check for things that check needs/wants and that we don't check for already
dnl AM_GST_CHECK_CHECKS()

AC_DEFUN([AG_GST_CHECK_CHECKS],
[
AC_MSG_NOTICE([Running check unit test framework checks now...])

CHECK_MAJOR_VERSION=0
CHECK_MINOR_VERSION=9
CHECK_MICRO_VERSION=8
CHECK_VERSION=$CHECK_MAJOR_VERSION.$CHECK_MINOR_VERSION.$CHECK_MICRO_VERSION

AC_SUBST(CHECK_MAJOR_VERSION)
AC_SUBST(CHECK_MINOR_VERSION)
AC_SUBST(CHECK_MICRO_VERSION)
AC_SUBST(CHECK_VERSION)

dnl Checks for header files and declarations
AC_CHECK_HEADERS([unistd.h sys/wait.h sys/time.h])

AC_CHECK_FUNCS([localtime_r])


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
