# Configure paths for Tremor

dnl XIPH_PATH_IVORBIS([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libivorbis, and define IVORBIS_CFLAGS and IVORBIS_LIBS
dnl
AC_DEFUN([XIPH_PATH_IVORBIS],
[dnl 
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(ivorbis,[  --with-ivorbis=PFX   Prefix where libivorbis is installed (optional)], ivorbis_prefix="$withval", ivorbis_prefix="")
AC_ARG_WITH(ivorbis-libraries,[  --with-ivorbis-libraries=DIR   Directory where libivorbis library is installed (optional)], ivorbis_libraries="$withval", ivorbis_libraries="")
AC_ARG_WITH(ivorbis-includes,[  --with-ivorbis-includes=DIR   Directory where libivorbis header files are installed (optional)], ivorbis_includes="$withval", ivorbis_includes="")
AC_ARG_ENABLE(ivorbistest, [  --disable-ivorbistest       Do not try to compile and run a test Ivorbis program],, enable_ivorbistest=yes)

  if test "x$ivorbis_libraries" != "x" ; then
    IVORBIS_LIBS="-L$ivorbis_libraries"
  elif test "x$ivorbis_prefix" != "x" ; then
    IVORBIS_LIBS="-L$ivorbis_prefix/lib"
  elif test "x$prefix" != "xNONE"; then
    IVORBIS_LIBS="-L$prefix/lib"
  fi

  IVORBIS_LIBS="$IVORBIS_LIBS -lvorbisidec -lm"

  if test "x$ivorbis_includes" != "x" ; then
    IVORBIS_CFLAGS="-I$ivorbis_includes"
  elif test "x$ivorbis_prefix" != "x" ; then
    IVORBIS_CFLAGS="-I$ivorbis_prefix/include"
  elif test "x$prefix" != "xNONE"; then
    IVORBIS_CFLAGS="-I$prefix/include"
  fi

  AC_MSG_CHECKING(for Tremor)
  no_ivorbis=""

  if test "x$enable_ivorbistest" = "xyes" ; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $IVORBIS_CFLAGS $OGG_CFLAGS"
    LIBS="$LIBS $IVORBIS_LIBS $OGG_LIBS"
dnl
dnl Now check if the installed Tremor is sufficiently new.
dnl
      rm -f conf.ivorbistest
      AC_TRY_COMPILE([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tremor/codec.h>
],,, no_ivorbis=yes)
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_ivorbis" = "x" ; then
     AC_MSG_RESULT(yes)
     ifelse([$1], , :, [$1])     
  else
     AC_MSG_RESULT(no)
     IVORBIS_CFLAGS=""
     IVORBIS_LIBS=""
     IVORBISFILE_LIBS=""
     ifelse([$2], , :, [$2])
  fi
  AC_SUBST(IVORBIS_CFLAGS)
  AC_SUBST(IVORBIS_LIBS)
  AC_SUBST(IVORBISFILE_LIBS)
])
