# Configure paths for libebml

dnl PATH_EBML([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libebml, and define EBML_CFLAGS and EBML_LIBS
dnl
AC_DEFUN([PATH_EBML],
[dnl 
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(ebml-prefix,[  --with-ebml-prefix=PFX        Prefix where libebml is installed (optional)], ebml_prefix="$withval", ebml_prefix="")
AC_ARG_WITH(ebml-include,[  --with-ebml-include=DIR       Path to where the libebml include files installed (optional)], ebml_include="$withval", ebml_include="")
AC_ARG_WITH(ebml-lib,[  --with-ebml-lib=DIR           Path to where the libebml library installed (optional)], ebml_lib="$withval", ebml_lib="")
AC_ARG_ENABLE(ebmltest, [  --disable-ebmltest            Do not try to compile and run a test EBML program],, enable_ebmltest=yes)

  if test "x$ebml_prefix" != "x"; then
    ebml_args="$ebml_args --prefix=$ebml_prefix"
    if test "x$ebml_include" != "x"; then
      EBML_CFLAGS="-I$ebml_include"
    else
      EBML_CFLAGS="-I$ebml_prefix/include/ebml"
    fi
    if test "x$ebml_lib" != "x"; then
      EBML_LIBS="-L$ebml_lib"
    else
      EBML_LIBS="-L$ebml_prefix/lib"
    fi
  elif test "x$prefix" != "xNONE"; then
    ebml_args="$ebml_args --prefix=$prefix"
    if test "x$ebml_include" != "x"; then
      EBML_CFLAGS="-I$ebml_include"
    else
      EBML_CFLAGS="-I$prefix/include/ebml"
    fi
    if test "x$ebml_lib" != "x"; then
      EBML_LIBS="-L$ebml_lib"
    else
      EBML_LIBS="-L$prefix/lib"
    fi
  else
    if test "x$ebml_include" != "x"; then
      EBML_CFLAGS="-I$ebml_include"
    else
      EBML_CFLAGS="-I/usr/include/ebml -I/usr/local/include/ebml"
    fi
    if test "x$ebml_lib" != "x"; then
      EBML_LIBS="-L$ebml_lib"
    else
      EBML_LIBS="-L/usr/local/lib"
    fi
  fi

  EBML_LIBS="$EBML_LIBS -lebml"

  AC_MSG_CHECKING(for EBML)
  no_ebml=""


  if test "x$enable_ebmltest" = "xyes" ; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $EBML_CFLAGS"
    LIBS="$LIBS $EBML_LIBS"
dnl
dnl Now check if the installed EBML is sufficiently new.
dnl
      rm -f conf.ebmltest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EbmlConfig.h>

int main ()
{
  system("touch conf.ebmltest");
  return 0;
}

],, no_ebml=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
  fi

  if test "x$no_ebml" = "x" ; then
     AC_MSG_RESULT(yes)
     ifelse([$1], , :, [$1])     
  else
     AC_MSG_RESULT(no)
     if test -f conf.ebmltest ; then
       :
     else
       echo "*** Could not run Ebml test program, checking why..."
       CFLAGS="$CFLAGS $EBML_CFLAGS"
       LIBS="$LIBS $EBML_LIBS"
       AC_TRY_LINK([
#include <stdio.h>
#include <EbmlConfig.h>
],     [ return 0; ],
       [ echo "*** The test program compiled, but did not run. This usually means"
       echo "*** that the run-time linker is not finding EBML or finding the wrong"
       echo "*** version of EBML. If it is not finding EBML, you'll need to set your"
       echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
       echo "*** to the installed location  Also, make sure you have run ldconfig if that"
       echo "*** is required on your system"
       echo "***"
       echo "*** If you have an old version installed, it is best to remove it, although"
       echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
       [ echo "*** The test program failed to compile or link. See the file config.log for the"
       echo "*** exact error that occured. This usually means EBML was incorrectly installed"
       echo "*** or that you have moved EBML since it was installed." ])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
     EBML_CFLAGS=""
     EBML_LIBS=""
     ifelse([$2], , :, [$2])
  fi
  AC_SUBST(EBML_CFLAGS)
  AC_SUBST(EBML_LIBS)
  rm -f conf.ebmltest
])

# Configure paths for libmatroska

dnl PATH_MATROSKA(MIN_VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test for libmatroska, and define MATROSKA_CFLAGS and MATROSKA_LIBS
dnl
AC_DEFUN([PATH_MATROSKA],
[dnl 
dnl Get the cflags and libraries
dnl
AC_ARG_WITH(matroska-prefix,[  --with-matroska-prefix=PFX    Prefix where libmatroska is installed (optional)], matroska_prefix="$withval", matroska_prefix="")
AC_ARG_WITH(matroska-include,[  --with-matroska-include=DIR   Path to where the libmatroska include files installed (optional)], matroska_include="$withval", matroska_include="")
AC_ARG_WITH(matroska-lib,[  --with-matroska-lib=DIR       Path to where the libmatroska library installed (optional)], matroska_lib="$withval", matroska_lib="")
AC_ARG_ENABLE(matroskatest, [  --disable-matroskatest        Do not try to compile and run a test Matroska program],, enable_matroskatest=yes)

  if test "x$matroska_prefix" != "x"; then
    matroska_args="$matroska_args --prefix=$matroska_prefix"
    if test "x$matroska_include" != "x"; then
      MATROSKA_CFLAGS="-I$matroska_include"
    else
      MATROSKA_CFLAGS="-I$matroska_prefix/include/matroska"
    fi
    if test "x$matroska_lib" != "x"; then
      MATROSKA_LIBS="-L$matroska_lib"
    else
      MATROSKA_LIBS="-L$matroska_prefix/lib"
    fi
  elif test "x$prefix" != "xNONE"; then
    matroska_args="$matroska_args --prefix=$prefix"
    if test "x$matroska_include" != "x"; then
      MATROSKA_CFLAGS="-I$matroska_include"
    else
      MATROSKA_CFLAGS="-I$prefix/include/matroska"
    fi
    if test "x$matroska_lib" != "x"; then
      MATROSKA_LIBS="-L$matroska_lib"
    else
      MATROSKA_LIBS="-L$prefix/lib"
    fi
  else
    if test "x$matroska_include" != "x"; then
      MATROSKA_CFLAGS="-I$matroska_include"
    else
      MATROSKA_CFLAGS="-I/usr/include/matroska -I/usr/local/include/matroska"
    fi
    if test "x$matroska_lib" != "x"; then
      MATROSKA_LIBS="-L$matroska_lib"
    else
      MATROSKA_LIBS="-L/usr/local/lib"
    fi
  fi

  MATROSKA_LIBS="$MATROSKA_LIBS -lmatroska"

  AC_MSG_CHECKING(for Matroska)
  no_matroska=""


  if test "x$enable_matroskatest" = "xyes" ; then
    ac_save_CXXFLAGS="$CXXFLAGS"
    ac_save_LIBS="$LIBS"
    CXXFLAGS="$CXXFLAGS $MATROSKA_CFLAGS $EBML_CFLAGS"
    LIBS="$LIBS $MATROSKA_LIBS $EBML_LIBS"
dnl
dnl Now check if the installed Matroska is sufficiently new.
dnl
      rm -f conf.matroskatest
      AC_LANG_CPLUSPLUS
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EbmlConfig.h>
#include <KaxVersion.h>

using namespace LIBMATROSKA_NAMESPACE;

int main ()
{
  FILE *f;
  f = fopen("conf.matroskatest", "wb");
  if (f == NULL)
    return 1;
  fprintf(f, "%s\n", KaxCodeVersion.c_str());
  fclose(f);
  return 0;
}

],, no_matroska=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
      AC_LANG_C
      CXXFLAGS="$ac_save_CXXFLAGS"
      LIBS="$ac_save_LIBS"
  fi

  if test "x$no_matroska" = "x" -a -f conf.matroskatest ; then
    AC_MSG_RESULT(yes)

    AC_MSG_CHECKING(Matroska version)

    matroska_version=`cat conf.matroskatest`
    mk_MAJVER=`echo $1 | cut -d"." -f1`
    mk_MINVER=`echo $1 | cut -d"." -f2`
    mk_RELVER=`echo $1 | cut -d"." -f3`
    mver_ok=`sed 's;\.;\ ;g' < conf.matroskatest | (read -a mver
    if test ${mver[[0]]} -gt $mk_MAJVER ; then
      mver_ok=1
    elif test ${mver[[0]]} -lt $mk_MAJVER ; then
      mver_ok=0
    else
      if test ${mver[[1]]} -gt $mk_MINVER ; then
        mver_ok=1
      elif test ${mver[[1]]} -lt $mk_MINVER ; then
        mver_ok=0
      else
        if test ${mver[[2]]} -ge $mk_RELVER ; then
          mver_ok=1
        else
          mver_ok=0
        fi
      fi
    fi
    echo $mver_ok )`
    if test "$mver_ok" = "1" ; then
      AC_MSG_RESULT($matroska_version ok)
       ifelse([$2], , :, [$2])     
    else
      AC_MSG_RESULT($matroska_version too old)
      echo '*** Your Matroska version is too old. Upgrade to at least version'
      echo '*** $1 and re-run configure.'
       ifelse([$3], , :, [$3])     
    fi

  else
     AC_MSG_RESULT(no)
     ifelse([$3], , :, [$3])
  fi

  AC_SUBST(MATROSKA_CFLAGS)
  AC_SUBST(MATROSKA_LIBS)
  rm -f conf.matroskatest
])
