dnl as-arts.m4 0.1.0

dnl $Id: as-arts.m4,v 1.5 2004/05/21 11:20:49 thomasvs Exp $
dnl if you copy this file to your cvs,
dnl add this file using cvs -ko add to retain this header

dnl This is an example arts .m4 adapted and scrubbed by thomasvs

# Configure paths for ARTS
# Philip Stadermann   2001-06-21
# stolen from esd.m4

dnl AM_PATH_ARTS([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl Test for ARTS, and define ARTS_CXXFLAGS and ARTS_LIBS
dnl
AC_DEFUN([AM_PATH_ARTS],
[dnl 
dnl Get the cflags and libraries from the artsc-config script
dnl
AC_ARG_WITH(arts-prefix,
  AC_HELP_STRING([--with-arts-prefix=PFX],
                 [prefix where ARTS is installed (optional)]),
  arts_prefix="$withval", arts_prefix="")

AC_ARG_WITH(arts-exec-prefix,
  AC_HELP_STRING([--with-arts-exec-prefix=PFX],
                 [exec prefix where ARTS is installed (optional)]),
  arts_exec_prefix="$withval", arts_exec_prefix="")

AC_ARG_ENABLE(artstest, 
  AC_HELP_STRING([--disable-artstest],
                 [do not try to compile and run a test ARTS program]),
  , enable_artstest=yes)

  if test x$arts_exec_prefix != x ; then
    arts_args="$arts_args --exec-prefix=$arts_exec_prefix"
    if test x${ARTS_CONFIG+set} != xset ; then
      ARTS_CONFIG=$arts_exec_prefix/bin/artsc-config
    fi
  fi
  if test x$arts_prefix != x ; then
    arts_args="$arts_args --prefix=$arts_prefix"
    if test x${ARTS_CONFIG+set} != xset ; then
      ARTS_CONFIG=$arts_prefix/bin/artsc-config
    fi
  fi

  AC_PATH_PROG(ARTS_CONFIG, artsc-config, no)
  min_arts_version=ifelse([$1], ,0.9.5,$1)
  AC_MSG_CHECKING(for ARTS artsc - version >= $min_arts_version)
  no_arts=""
  if test "$ARTS_CONFIG" = "no" ; then
    no_arts=yes
  else
    # FIXME: thomas added this sed to get arts path instead of artsc
    # replace -I.../artsc with -I.../arts
    ARTS_CXXFLAGS=`$ARTS_CONFIG $artsconf_args --cflags | artsc-config --cflags | sed 's/\(-I.*\)artsc/\1arts/'`
    ARTS_LIBS=`$ARTS_CONFIG $artsconf_args --libs | sed 's/artsc$/arts/'`

    arts_major_version=`$ARTS_CONFIG $arts_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
    arts_minor_version=`$ARTS_CONFIG $arts_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
    arts_micro_version=`$ARTS_CONFIG $arts_config_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`
    if test "x$enable_artstest" = "xyes" ; then
      dnl ac_save_CXXFLAGS="$CXXFLAGS"
      dnl ac_save_LIBS="$LIBS"
      dnl CFLAGS="$CFLAGS $ARTS_CXXFLAGS"
      dnl LIBS="$LIBS $ARTS_LIBS"
dnl
dnl Now check if the installed ARTS is sufficiently new. (Also sanity
dnl checks the results of artsc-config to some extent)
dnl

dnl a*s: to successfully compile the C++ test app, we need to 
dnl first make sure we're going to compile it as C++ (with AC_LANG_PUSH),
dnl then add the CFLAGS and CLIBS of arts which we just discovered to the
dnl C++ compilation and linking flags.
dnl We also need to clean up after the test; this means using AC_LANG_POP
dnl and restoring the CPPFLAGS and LDFLAGS from the saved values we take 
dnl here.

dnl ask nicely for C++ compilation
      AC_LANG_PUSH(C++)

dnl save compilation and link flags and make our own
      ac_save_CPPFLAGS="$CPPFLAGS"
      ac_save_LDFLAGS="$LDFLAGS"
      AC_SUBST(CPPFLAGS,"$CPPFLAGS $ARTS_CXXFLAGS")
      AC_SUBST(LDFLAGS,"$LDFLAGS $ARTS_CLIBS")
 
      rm -f conf.artstest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <artsflow.h>

char*
my_strdup (char *str)
{
  char *new_str;
  
  if (str)
    {
      // thomas: the original test did not have the typecast
      new_str = (char *) malloc ((strlen (str) + 1) * sizeof(char));
      strcpy (new_str, str);
    }
  else
    new_str = NULL;
  
  return new_str;
}

int main ()
{
  int major, minor, micro;
  char *tmp_version;

  system ("touch conf.artstest");

  /* HP/UX 9 (%@#!) writes to sscanf strings */
  tmp_version = my_strdup("$min_arts_version");
  if (sscanf(tmp_version, "%d.%d.%d", &major, &minor, &micro) != 3) {
     printf("%s, bad version string\n", "$min_arts_version");
     exit(1);
   }

   if (($arts_major_version > major) ||
      (($arts_major_version == major) && ($arts_minor_version > minor)) ||
      (($arts_major_version == major) && ($arts_minor_version == minor) && ($arts_micro_version >= micro)))
    {
      return 0;
    }
  else
    {
      printf("\n*** 'artsc-config --version' returned %d.%d.%d, but the minimum version\n", $arts_major_version, $arts_minor_version, $arts_micro_version);
      printf("*** of ARTS required is %d.%d.%d. If artsc-config is correct, then it is\n", major, minor, micro);
      printf("*** best to upgrade to the required version.\n");
      printf("*** If artsc-config was wrong, set the environment variable ARTS_CONFIG\n");
      printf("*** to point to the correct copy of artsc-config, and remove the file\n");
      printf("*** config.cache before re-running configure\n");
      return 1;
    }
}

],, no_arts=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
      dnl CFLAGS="$ac_save_CFLAGS"
      dnl LIBS="$ac_save_LIBS"
      dnl a*s this is were we clean up after the test
      AC_LANG_POP(C++)
      CXXFLAGS="$ac_save_CXXFLAGS"
      LDFLAGS="$ac_save_LDFLAGS"
      dnl a*s we are sure that these are right, so we make them active
      AC_SUBST(CXXFLAGS,"$CXXFLAGS")
      AC_SUBST(LDFLAGS,"$LDFLAGS")
    fi
  fi
  if test "x$no_arts" = x ; then
    AC_MSG_RESULT(yes)
    ifelse([$2], , :, [$2])     
  else
    AC_MSG_RESULT(no)
    if test "$ARTS_CONFIG" = "no" ; then
      echo "*** The artsc-config script installed by ARTS could not be found"
      echo "*** If ARTS was installed in PREFIX, make sure PREFIX/bin is in"
      echo "*** your path, or set the ARTS_CONFIG environment variable to the"
      echo "*** full path to artsc-config."
    else
      if test -f conf.artstest ; then
        :
      else
         echo "*** Could not run ARTS test program, checking why..."
         CFLAGS="$CFLAGS $ARTS_CXXFLAGS"
         LIBS="$LIBS $ARTS_LIBS"
         AC_TRY_LINK([
#include <stdio.h>
#include <artsflow.h>
],      [ return 0; ],
        [ echo "*** The test program compiled, but did not run. This usually means"
          echo "*** that the run-time linker is not finding ARTS or finding the wrong"
          echo "*** version of ARTS. If it is not finding ARTS, you'll need to set your"
          echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
          echo "*** to the installed location  Also, make sure you have run ldconfig if that"
          echo "*** is required on your system"
	  echo "***"
          echo "*** If you have an old version installed, it is best to remove it, although"
          echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
        [ echo "*** The test program failed to compile or link. See the file config.log for the"
          echo "*** exact error that occured. This usually means ARTS was incorrectly installed"
          echo "*** or that you have moved ARTS since it was installed. In the latter case, you"
          echo "*** may want to edit the artsc-config script: $ARTS_CONFIG" ])
          CFLAGS="$ac_save_CFLAGS"
          LIBS="$ac_save_LIBS"
      fi
    fi
    ARTS_CXXFLAGS=""
    ARTS_LIBS=""
    ifelse([$3], , :, [$3])
  fi
  AC_SUBST(ARTS_CXXFLAGS)
  AC_SUBST(ARTS_LIBS)
  rm -f conf.artstest
])

dnl release C++ question

