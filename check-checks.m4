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

# Checks for header files.

# Create _stdint.h in the top-level directory
AX_CREATE_STDINT_H

])
