# Configure paths for libshout
# Jack Moffitt <jack@icecast.org> 08-06-2001
# Shamelessly stolen from Owen Taylor and Manish Singh

dnl AM_PATH_SHOUT2([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libshout, and define SHOUT2_CFLAGS and SHOUT2_LIBS
dnl
AC_DEFUN([AM_PATH_SHOUT2],
[dnl 
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(shout-prefix,[  --with-shout2-prefix=PFX   Prefix where libshout2 is installed (optional)], shout2_prefix="$withval", shout2_prefix="")
AC_ARG_ENABLE(shout2test, [  --disable-shout2test       Do not try to compile and run a test Shout2 program],, enable_shout2test=yes)

  if test "x$shout2_prefix" != "xNONE" ; then
    shout2_args="$shout2_args --prefix=$shout2_prefix"
    SHOUT2_CFLAGS="-I$shout2_prefix/include"
    SHOUT2_LIBS="-L$shout2_prefix/lib"
  elif test "$prefix" != ""; then
    shout2_args="$shout2_args --prefix=$prefix"
    SHOUT2_CFLAGS="-I$prefix/include"
    SHOUT2_LIBS="-L$prefix/lib"
  fi

  SHOUT2_LIBS="$SHOUT2_LIBS -lshout -lpthread"

  case $host in
  *-*-solaris*)
  	SHOUT2_LIBS="$SHOUT2_LIBS -lnsl -lsocket -lresolv"
  esac

  AC_MSG_CHECKING(for Shout2)
  no_shout2=""

  if test "x$enable_shout2test" = "xyes" ; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $SHOUT2_CFLAGS $OGG_CFLAGS $VORBIS_CFLAGS"
    LIBS="$LIBS $SHOUT2_LIBS $OGG_LIBS $VORBIS_LIBS"
dnl
dnl Now check if the installed Shout2 is sufficiently new.
dnl
      rm -f conf.shout2test
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shout/shout.h>

int main ()
{
  system("touch conf.shout2test");
  return 0;
}

],, no_shout2=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_shout2" = "x" ; then
     AC_MSG_RESULT(yes)
     ifelse([$1], , :, [$1])     
  else
     AC_MSG_RESULT(no)
     if test -f conf.shout2test ; then
       :
     else
       echo "*** Could not run Shout2 test program, checking why..."
       CFLAGS="$CFLAGS $SHOUT2_CFLAGS $OGG_CFLAGS $VORBIS_CFLAGS"
       LIBS="$LIBS $SHOUT2_LIBS $OGG_LIBS $VORBIS_LIBS"
       AC_TRY_LINK([
#include <stdio.h>
#include <shout/shout.h>
],     [ return 0; ],
       [ echo "*** The test program compiled, but did not run. This usually means"
       echo "*** that the run-time linker is not finding Shout2 or finding the wrong"
       echo "*** version of Shout2. If it is not finding Shout2, you'll need to set your"
       echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
       echo "*** to the installed location  Also, make sure you have run ldconfig if that"
       echo "*** is required on your system"
       echo "***"
       echo "*** If you have an old version installed, it is best to remove it, although"
       echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
       [ echo "*** The test program failed to compile or link. See the file config.log for the"
       echo "*** exact error that occured. This usually means Shout2 was incorrectly installed"
       echo "*** or that you have moved Shout2 since it was installed. In the latter case, you"
       echo "*** may want to edit the shout-config script: $SHOUT2_CONFIG" ])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
     SHOUT2_CFLAGS=""
     SHOUT2_LIBS=""
     ifelse([$2], , :, [$2])
  fi
  AC_SUBST(SHOUT2_CFLAGS)
  AC_SUBST(SHOUT2_LIBS)
  rm -f conf.shout2test
])
