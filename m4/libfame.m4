dnl AM_PATH_LIBFAME([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND [, MODULES]]]])
dnl Test for libfame, and define LIBFAME_CFLAGS and LIBFAME_LIBS
dnl Vivien Chappelier 12/11/00
dnl stolen from ORBit autoconf
dnl
AC_DEFUN([AM_PATH_LIBFAME],
[dnl 
dnl Get the cflags and libraries from the libfame-config script
dnl
AC_ARG_WITH(libfame-prefix,[  --with-libfame-prefix=PFX   Prefix where libfame is installed (optional)],
            libfame_config_prefix="$withval", libfame_config_prefix="")
AC_ARG_WITH(libfame-exec-prefix,[  --with-libfame-exec-prefix=PFX Exec prefix where libfame is installed (optional)],
            libfame_config_exec_prefix="$withval", libfame_config_exec_prefix="")
AC_ARG_ENABLE(libfametest, [  --disable-libfametest       Do not try to compile and run a test libfame program],
		    , enable_libfametest=yes)

  if test x$libfame_config_exec_prefix != x ; then
     libfame_config_args="$libfame_config_args --exec-prefix=$libfame_config_exec_prefix"
     if test x${LIBFAME_CONFIG+set} != xset ; then
        LIBFAME_CONFIG=$libfame_config_exec_prefix/bin/libfame-config
     fi
  fi
  if test x$libfame_config_prefix != x ; then
     libfame_config_args="$libfame_config_args --prefix=$libfame_config_prefix"
     if test x${LIBFAME_CONFIG+set} != xset ; then
        LIBFAME_CONFIG=$libfame_config_prefix/bin/libfame-config
     fi
  fi

  AC_PATH_PROG(LIBFAME_CONFIG, libfame-config, no)
  min_libfame_version=ifelse([$1], , 0.9.0, $1)
  AC_MSG_CHECKING(for libfame - version >= $min_libfame_version)
  no_libfame=""
  if test "$LIBFAME_CONFIG" = "no" ; then
    no_libfame=yes
  else
    LIBFAME_CFLAGS=`$LIBFAME_CONFIG $libfame_config_args --cflags`
    LIBFAME_LIBS=`$LIBFAME_CONFIG $libfame_config_args --libs`
    libfame_config_major_version=`$LIBFAME_CONFIG $libfame_config_args --version | \
	   sed -e 's,[[^0-9.]],,g' -e 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
    libfame_config_minor_version=`$LIBFAME_CONFIG $libfame_config_args --version | \
	   sed -e 's,[[^0-9.]],,g' -e 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
    libfame_config_micro_version=`$LIBFAME_CONFIG $libfame_config_args --version | \
	   sed -e 's,[[^0-9.]],,g' -e 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`
    if test "x$enable_libfametest" = "xyes" ; then
      ac_save_CFLAGS="$CFLAGS"
      ac_save_LIBS="$LIBS"
      CFLAGS="$CFLAGS $LIBFAME_CFLAGS"
      LIBS="$LIBFAME_LIBS $LIBS"
dnl
dnl Now check if the installed LIBFAME is sufficiently new. (Also sanity
dnl checks the results of libfame-config to some extent
dnl
      rm -f conf.libfametest
      AC_TRY_RUN([
#include <fame.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int 
main ()
{
  int major, minor, micro;
  char *tmp_version;

  system ("touch conf.libfametest");

  /* HP/UX 9 (%@#!) writes to sscanf strings */
  tmp_version = strdup("$min_libfame_version");
  if (sscanf(tmp_version, "%d.%d.%d", &major, &minor, &micro) != 3) {
     printf("%s, bad version string\n", "$min_libfame_version");
     exit(1);
   }

  if ((libfame_major_version != $libfame_config_major_version) ||
      (libfame_minor_version != $libfame_config_minor_version) ||
      (libfame_micro_version != $libfame_config_micro_version))
    {
      printf("\n*** 'libfame-config --version' returned %d.%d.%d, but Libfame (%d.%d.%d)\n", 
             $libfame_config_major_version, $libfame_config_minor_version, $libfame_config_micro_version,
             libfame_major_version, libfame_minor_version, libfame_micro_version);
      printf ("*** was found! If libfame-config was correct, then it is best\n");
      printf ("*** to remove the old version of libfame. You may also be able to fix the error\n");
      printf("*** by modifying your LD_LIBRARY_PATH enviroment variable, or by editing\n");
      printf("*** /etc/ld.so.conf. Make sure you have run ldconfig if that is\n");
      printf("*** required on your system.\n");
      printf("*** If libfame-config was wrong, set the environment variable LIBFAME_CONFIG\n");
      printf("*** to point to the correct copy of libfame-config, and remove the file config.cache\n");
      printf("*** before re-running configure\n");
    } 
#if defined (LIBFAME_MAJOR_VERSION) && defined (LIBFAME_MINOR_VERSION) && defined (LIBFAME_MICRO_VERSION)
  else if ((libfame_major_version != LIBFAME_MAJOR_VERSION) ||
	   (libfame_minor_version != LIBFAME_MINOR_VERSION) ||
           (libfame_micro_version != LIBFAME_MICRO_VERSION))
    {
      printf("*** libfame header files (version %d.%d.%d) do not match\n",
	     LIBFAME_MAJOR_VERSION, LIBFAME_MINOR_VERSION, LIBFAME_MICRO_VERSION);
      printf("*** library (version %d.%d.%d)\n",
	     libfame_major_version, libfame_minor_version, libfame_micro_version);
    }
#endif /* defined (LIBFAME_MAJOR_VERSION) ... */
  else
    {
      if ((libfame_major_version > major) ||
        ((libfame_major_version == major) && (libfame_minor_version > minor)) ||
        ((libfame_major_version == major) && (libfame_minor_version == minor) && (libfame_micro_version >= micro)))
      {
        return 0;
       }
     else
      {
        printf("\n*** An old version of libfame (%d.%d.%d) was found.\n",
               libfame_major_version, libfame_minor_version, libfame_micro_version);
        printf("*** You need a version of libfame newer than %d.%d.%d. The latest version of\n",
	       major, minor, micro);
        printf("*** libfame is always available from http://www-eleves.enst-bretagne.fr/~chappeli/fame\n");
        printf("***\n");
        printf("*** If you have already installed a sufficiently new version, this error\n");
        printf("*** probably means that the wrong copy of the libfame-config shell script is\n");
        printf("*** being found. The easiest way to fix this is to remove the old version\n");
        printf("*** of libfame, but you can also set the LIBFAME_CONFIG environment to point to the\n");
        printf("*** correct copy of libfame-config. (In this case, you will have to\n");
        printf("*** modify your LD_LIBRARY_PATH enviroment variable, or edit /etc/ld.so.conf\n");
        printf("*** so that the correct libraries are found at run-time))\n");
      }
    }
  return 1;
}
],, no_libfame=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
  fi
  if test "x$no_libfame" = x ; then
     AC_MSG_RESULT(yes)
     ifelse([$2], , :, [$2])     
  else
     AC_MSG_RESULT(no)
     if test "$LIBFAME_CONFIG" = "no" ; then
       echo "*** The libfame-config script installed by libfame could not be found"
       echo "*** If libfame was installed in PREFIX, make sure PREFIX/bin is in"
       echo "*** your path, or set the LIBFAME_CONFIG environment variable to the"
       echo "*** full path to libfame-config."
     else
       if test -f conf.libfametest ; then
        :
       else
          echo "*** Could not run libfame test program, checking why..."
          CFLAGS="$CFLAGS $LIBFAME_CFLAGS"
          LIBS="$LIBS $LIBFAME_LIBS"
          AC_TRY_LINK([
#include <fame.h>
#include <stdio.h>
],      [ return ((libfame_major_version) || (libfame_minor_version) || (libfame_micro_version)); ],
        [ echo "*** The test program compiled, but did not run. This usually means"
          echo "*** that the run-time linker is not finding libfame or finding the wrong"
          echo "*** version of LIBFAME. If it is not finding libfame, you'll need to set your"
          echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
          echo "*** to the installed location  Also, make sure you have run ldconfig if that"
          echo "*** is required on your system"
	  echo "***"
          echo "*** If you have an old version installed, it is best to remove it, although"
          echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"
          echo "***" ],
        [ echo "*** The test program failed to compile or link. See the file config.log for the"
          echo "*** exact error that occured. This usually means libfame was incorrectly installed"
          echo "*** or that you have moved libfame since it was installed. In the latter case, you"
          echo "*** may want to edit the libfame-config script: $LIBFAME_CONFIG" ])
          CFLAGS="$ac_save_CFLAGS"
          LIBS="$ac_save_LIBS"
       fi
     fi
     LIBFAME_CFLAGS=""
     LIBFAME_LIBS=""
     ifelse([$3], , :, [$3])
  fi

  AC_SUBST(LIBFAME_CFLAGS)
  AC_SUBST(LIBFAME_LIBS)
  rm -f conf.libfametest
])
