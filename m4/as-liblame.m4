# CFLAGS and library paths for LIBLAME
# taken from Autostar Sandbox, http://autostars.sourceforge.net/
# inspired by xmms.m4

dnl Usage:
dnl AM_PATH_LIBLAME([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl FIXME: version checking does not work currently
dnl
dnl Example:
dnl AM_PATH_LIBLAME(3.89, , AC_MSG_ERROR([*** LIBLAME >= 3.89 not installed)) 
dnl
dnl Defines LIBLAME_LIBS
dnl FIXME: should define LIBLAME_VERSION
dnl

AC_DEFUN([AM_PATH_LIBLAME],
[
  dnl check for the library
  AC_CHECK_LIB(mp3lame, lame_init, HAVE_LIBLAME=yes, HAVE_LIBLAME=no, -lm)
  dnl check if lame.h is available in the standard location or not
  HAVE_LAME_H_STD=no
  AC_CHECK_HEADER(lame.h, HAVE_LAME_H_STD=no, :)
  AC_CHECK_HEADER(lame/lame.h, HAVE_LAME_H_STD=yes, :)
  AC_MSG_CHECKING(for lame.h in right location)
  if test "x$HAVE_LAME_H_STD" = "xyes"; then
    AC_MSG_RESULT(yes)
  else
    AC_MSG_RESULT(no)
    HAVE_LIBLAME=no
    if test "x$HAVE_LAME_H_STD"="xno"; then
      AC_MSG_WARN(lame.h found in include dir,)
      AC_MSG_WARN( while it should be in it's own lame/ dir !)
    fi
 fi

  dnl now do the actual "do we have it ?" test
  if test "x$HAVE_LIBLAME" = "xyes"; then
    LIBLAME_LIBS="-lmp3lame -lm"
    dnl execute what we have to because it's found
    ifelse([$2], , :, [$2])
  else
    LIBLAME_LIBS=""
    dnl execute what we have to because it's not found
    ifelse([$3], , :, [$3])
  fi

  dnl make variables available
  AC_SUBST(LIBLAME_LIBS)
  AC_SUBST(HAVE_LIBLAME)
])
