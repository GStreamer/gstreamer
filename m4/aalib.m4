# Configure paths for AALIB
# touched up for clean output by Thomas Vander Stichele
# Jan Hubicka 4/22/2001
# stolen from Sam Lantinga 9/21/99
# stolen from Manish Singh
# stolen back from Frank Belew
# stolen from Manish Singh
# Shamelessly stolen from Owen Taylor

dnl AM_PATH_AALIB([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl Test for AALIB, and define AALIB_CFLAGS and AALIB_LIBS
dnl
AC_DEFUN([AM_PATH_AALIB],
[dnl 
dnl Get the cflags and libraries from the aalib-config script
dnl
AC_ARG_WITH(aalib-prefix,
  AC_HELP_STRING([--with-aalib-prefix=PFX],
                 [prefix where AALIB is installed (optional)]),
  aalib_prefix="$withval", aalib_prefix="")

AC_ARG_WITH(aalib-exec-prefix,
  AC_HELP_STRING([--with-aalib-exec-prefix=PFX],
                 [exec prefix where AALIB is installed (optional)]),
  aalib_exec_prefix="$withval", aalib_exec_prefix="")

AC_ARG_ENABLE(aalibtest, 
  AC_HELP_STRING([--disable-aalibtest],
                 [do not try to compile and run a test AALIB program]),
  , enable_aalibtest=yes)

  if test x$aalib_exec_prefix != x ; then
     aalib_args="$aalib_args --exec-prefix=$aalib_exec_prefix"
     if test x${AALIB_CONFIG+set} != xset ; then
        AALIB_CONFIG=$aalib_exec_prefix/bin/aalib-config
     fi
  fi
  if test x$aalib_prefix != x ; then
     aalib_args="$aalib_args --prefix=$aalib_prefix"
     if test x${AALIB_CONFIG+set} != xset ; then
        AALIB_CONFIG=$aalib_prefix/bin/aalib-config
     fi
  fi

  AC_PATH_PROG(AALIB_CONFIG, aalib-config, no)
  min_aalib_version=ifelse([$1], ,0.11.0,$1)
  AC_MSG_CHECKING(for AALIB - version >= $min_aalib_version)
  no_aalib=""
  if test "$AALIB_CONFIG" = "no" ; then
    no_aalib=yes
  else
    AALIB_CFLAGS=`$AALIB_CONFIG $aalibconf_args --cflags`
    AALIB_LIBS=`$AALIB_CONFIG $aalibconf_args --libs`

    aalib_major_version=`$AALIB_CONFIG $aalib_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
    aalib_minor_version=`$AALIB_CONFIG $aalib_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
    aalib_micro_version=`$AALIB_CONFIG $aalib_config_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`
    if test "x$enable_aalibtest" = "xyes" ; then
      ac_save_CFLAGS="$CFLAGS"
      ac_save_LIBS="$LIBS"
      CFLAGS="$CFLAGS $AALIB_CFLAGS"
      LIBS="$LIBS $AALIB_LIBS"
dnl
dnl Now check if the installed AALIB is sufficiently new. (Also sanity
dnl checks the results of aalib-config to some extent
dnl
      rm -f conf.aalibtest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aalib.h"

char*
my_strdup (char *str)
{
  char *new_str;
  
  if (str)
    {
      new_str = (char *)malloc ((strlen (str) + 1) * sizeof(char));
      strcpy (new_str, str);
    }
  else
    new_str = NULL;
  
  return new_str;
}

int main (int argc, char *argv[])
{
  int major, minor, micro;
  char *tmp_version;

  /* This hangs on some systems (?)
  system ("touch conf.aalibtest");
  */
  { FILE *fp = fopen("conf.aalibtest", "a"); if ( fp ) fclose(fp); }

  /* HP/UX 9 (%@#!) writes to sscanf strings */
  tmp_version = my_strdup("$min_aalib_version");
  if (sscanf(tmp_version, "%d.%d.%d", &major, &minor, &micro) != 3) {
     printf("%s, bad version string\n", "$min_aalib_version");
     exit(1);
   }

   if (($aalib_major_version > major) ||
      (($aalib_major_version == major) && ($aalib_minor_version > minor)) ||
      (($aalib_major_version == major) && ($aalib_minor_version == minor) && ($aalib_micro_version >= micro)))
    {
      return 0;
    }
  else
    {
      printf("\n*** 'aalib-config --version' returned %d.%d.%d, but the minimum version\n", $aalib_major_version, $aalib_minor_version, $aalib_micro_version);
      printf("*** of AALIB required is %d.%d.%d. If aalib-config is correct, then it is\n", major, minor, micro);
      printf("*** best to upgrade to the required version.\n");
      printf("*** If aalib-config was wrong, set the environment variable AALIB_CONFIG\n");
      printf("*** to point to the correct copy of aalib-config, and remove the file\n");
      printf("*** config.cache before re-running configure\n");
      return 1;
    }
}

],, no_aalib=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
  fi
  if test "x$no_aalib" = x ; then
     AC_MSG_RESULT(yes)
     ifelse([$2], , :, [$2])     
  else
     AC_MSG_RESULT(no)
     if test "$AALIB_CONFIG" = "no" ; then
       echo "*** The aalib-config script installed by AALIB could not be found"
       echo "*** If AALIB was installed in PREFIX, make sure PREFIX/bin is in"
       echo "*** your path, or set the AALIB_CONFIG environment variable to the"
       echo "*** full path to aalib-config."
     else
       if test -f conf.aalibtest ; then
        :
       else
          echo "*** Could not run AALIB test program, checking why..."
          CFLAGS="$CFLAGS $AALIB_CFLAGS"
          LIBS="$LIBS $AALIB_LIBS"
          AC_TRY_LINK([
#include <stdio.h>
#include "AALIB.h"
],      [ return 0; ],
        [ echo "*** The test program compiled, but did not run. This usually means"
          echo "*** that the run-time linker is not finding AALIB or finding the wrong"
          echo "*** version of AALIB. If it is not finding AALIB, you'll need to set your"
          echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
          echo "*** to the installed location  Also, make sure you have run ldconfig if that"
          echo "*** is required on your system"
	  echo "***"
          echo "*** If you have an old version installed, it is best to remove it, although"
          echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
        [ echo "*** The test program failed to compile or link. See the file config.log for the"
          echo "*** exact error that occured. This usually means AALIB was incorrectly installed"
          echo "*** or that you have moved AALIB since it was installed. In the latter case, you"
          echo "*** may want to edit the aalib-config script: $AALIB_CONFIG" ])
          CFLAGS="$ac_save_CFLAGS"
          LIBS="$ac_save_LIBS"
       fi
     fi
     AALIB_CFLAGS=""
     AALIB_LIBS=""
     ifelse([$3], , :, [$3])
  fi
  AC_SUBST(AALIB_CFLAGS)
  AC_SUBST(AALIB_LIBS)
  rm -f conf.aalibtest
])
