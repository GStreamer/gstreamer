dnl check for sidplay

AC_DEFUN([GST_PATH_SIDPLAY],
[
AC_MSG_CHECKING([for libsidplay])

AC_LANG_PUSH(C++)

AC_CHECK_HEADER(sidplay/player.h, HAVE_SIDPLAY="yes", HAVE_SIDPLAY="no")

if test $HAVE_SIDPLAY = "yes"; then
  SIDPLAY_LIBS="-lsidplay"

  AC_MSG_CHECKING([whether -lsidplay works])
  ac_libs_safe=$LIBS

  LIBS="-lsidplay"

  AC_TRY_RUN([
    #include <sidplay/player.h>
    int main()
    { sidTune tune = sidTune(0);  }
    ],
    HAVE_SIDPLAY="yes",
    HAVE_SIDPLAY="no",
    HAVE_SIDPLAY="no")

  LIBS="$ac_libs_safe"

  AC_MSG_RESULT([$HAVE_SIDPLAY])
fi

SIDPLAY_CFLAGS=
SIDPLAY_LIBS="-lsidplay"
AC_SUBST(SIDPLAY_CFLAGS)
AC_SUBST(SIDPLAY_LIBS)

AC_LANG_POP(C++)
])
