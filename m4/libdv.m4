# Configure paths for libdv
# copied from vorbis.m4 by Thomas
# checks for libdv 0.9.5 since that added an extra argument to _init
# Shamelessly stolen from Owen Taylor and Manish Singh

dnl AM_PATH_LIBDV([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for liblibdv, and define LIBDV_CFLAGS and LIBDV_LIBS
dnl
AC_DEFUN([AM_PATH_LIBDV],
[dnl 
dnl Get the cflags and libraries

AC_ARG_WITH(libdv,[  --with-libdv=PFX   Prefix where libdv is installed (optional)], libdv_prefix="$withval", libdv_prefix="")
AC_ARG_WITH(libdv-libraries,[  --with-libdv-libraries=DIR   Directory where libdv library is installed (optional)], libdv_libraries="$withval", libdv_libraries="")
AC_ARG_WITH(libdv-includes,[  --with-libdv-includes=DIR   Directory where libdv header files are installed (optional)], libdv_includes="$withval", libdv_includes="")
AC_ARG_ENABLE(libdvtest, [  --disable-libdvtest       Do not try to compile and run a test libdv program],, enable_libdvtest=yes)

  if test "x$libdv_libraries" != "x" ; then
    LIBDV_LIBS="-L$libdv_libraries"
  elif test "x$libdv_prefix" != "x" ; then
    LIBDV_LIBS="-L$libdv_prefix/lib"
  elif test "x$prefix" != "xNONE"; then
    LIBDV_LIBS="-L$prefix/lib"
  fi

  LIBDV_LIBS="$LIBDV_LIBS -ldv -lm"

  if test "x$libdv_includes" != "x" ; then
    LIBDV_CFLAGS="-I$libdv_includes"
  elif test "x$libdv_prefix" != "x" ; then
    LIBDV_CFLAGS="-I$libdv_prefix/include"
  elif test "x$prefix" != "xNONE"; then
    LIBDV_CFLAGS="-I$prefix/include"
  fi


  AC_MSG_CHECKING(for libdv)
  no_libdv=""


  if test "x$enable_libdvtest" = "xyes" ; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $LIBDV_CFLAGS"
    LIBS="$LIBS $LIBDV_LIBS"
dnl
dnl Now check if the installed libdv is sufficiently new.
dnl
      dnl rm -f conf.libdvtest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libdv/dv.h>

int main ()
{
  dv_decoder_new (0, 0, 0);
  return 0;
}

],, no_libdv=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_libdv" = "x" ; then
     AC_MSG_RESULT(yes)
     ifelse([$1], , :, [$1])     
  else
     AC_MSG_RESULT(no)
     if test -f conf.libdvtest ; then
       :
     else
       echo "*** Could not run libdv test program, checking why..."
       CFLAGS="$CFLAGS $LIBDV_CFLAGS"
       LIBS="$LIBS $LIBDV_LIBS"
       AC_TRY_LINK([
#include <stdio.h>
#include <libdv/dv.h>
],     [ return 0; ],
       [ echo "*** The test program compiled, but did not run. This usually means"
       echo "*** that the run-time linker is not finding libdv or finding the wrong"
       echo "*** version of libdv. If it is not finding libdv, you'll need to set your"
       echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
       echo "*** to the installed location  Also, make sure you have run ldconfig if that"
       echo "*** is required on your system"
       echo "***"
       echo "*** If you have an old version installed, it is best to remove it, although"
       echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
       [ echo "*** The test program failed to compile or link. See the file config.log for the"
       echo "*** exact error that occured. This usually means libdv was incorrectly installed"
       echo "*** or that you have moved libdv since it was installed." ])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
     LIBDV_CFLAGS=""
     LIBDV_LIBS=""
     LIBDVFILE_LIBS=""
     LIBDVENC_LIBS=""
     ifelse([$2], , :, [$2])
  fi
  AC_SUBST(LIBDV_CFLAGS)
  AC_SUBST(LIBDV_LIBS)
  AC_SUBST(LIBDVFILE_LIBS)
  AC_SUBST(LIBDVENC_LIBS)
  dnl rm -f conf.libdvtest
])
