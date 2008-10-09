AC_DEFUN([AG_GST_VALGRIND_CHECK],
[
  dnl valgrind inclusion
  AC_ARG_ENABLE(valgrind,
    AC_HELP_STRING([--disable-valgrind], [disable run-time valgrind detection]),
    [
      case "${enableval}" in
        yes) USE_VALGRIND="$USE_DEBUG" ;;
        no)  USE_VALGRIND=no ;;
        *) AC_MSG_ERROR(bad value ${enableval} for --enable-valgrind) ;;
      esac],
    [
      USE_VALGRIND="$USE_DEBUG"
    ]) dnl Default value

  VALGRIND_REQ="2.1"
  if test "x$USE_VALGRIND" = xyes; then
    PKG_CHECK_MODULES(VALGRIND, valgrind > $VALGRIND_REQ,
      USE_VALGRIND="yes",
      [
        USE_VALGRIND="no"
        AC_MSG_RESULT([no])
      ])
  fi

  if test "x$USE_VALGRIND" = xyes; then
    AC_DEFINE(HAVE_VALGRIND, 1, [Define if valgrind should be used])
    AC_MSG_NOTICE(Using extra code paths for valgrind)
  fi
  AC_SUBST(VALGRIND_CFLAGS)
  AC_SUBST(VALGRIND_LIBS)
  
  AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
  AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")
])
