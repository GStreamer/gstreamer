dnl as-compiler-flag.m4 0.1.0

dnl autostars m4 macro for detection of compiler flags

dnl David Schleef <ds@schleef.org>

dnl $Id: as-compiler-flag.m4,v 1.1 2004/06/01 09:33:45 thomasvs Exp $

dnl AS_COMPILER_FLAG(CFLAGS, ACTION-IF-ACCEPTED, [ACTION-IF-NOT-ACCEPTED])
dnl Tries to compile with the given CFLAGS.
dnl Runs ACTION-IF-ACCEPTED if the compiler can compile with the flags,
dnl and ACTION-IF-NOT-ACCEPTED otherwise.

AC_DEFUN([AS_COMPILER_FLAG],
[
  AC_MSG_CHECKING([to see if compiler understands $1])

  save_CFLAGS="$CFLAGS"
  CFLAGS="$CFLAGS $1"

  AC_TRY_COMPILE([ ], [], [flag_ok=yes], [flag_ok=no])
  CFLAGS="$save_CFLAGS"

  if test "X$flag_ok" = Xyes ; then
    $2
    true
  else
    $3
    true
  fi
  AC_MSG_RESULT([$flag_ok])
])

