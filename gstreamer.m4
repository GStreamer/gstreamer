dnl Configure paths for GStreamer
dnl This was based upon the glib.m4 created by Owen Taylor 97-11-3
dnl Changes mostly involve replacing GLIB with GStreamer
dnl
dnl Written by Thomas Nyberg <thomas.nyberg@codefactory.se> 2001-03-01

dnl AM_PATH_GSTREAMER([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl Test for GStreamer, and define GSTREAMER_CFLAGS and GSTREAMER_LIBS
dnl
AC_DEFUN(AM_PATH_GSTREAMER,
[
dnl
dnl Get command-line stuff
dnl
	AC_ARG_WITH(gstreamer-prefix,[  --with-gstreamer-prefix=PFX   Prefix where GStreamer is installed (optional)],
 		gstreamer_config_prefix="$withval", gstreamer_config_prefix="")
	AC_ARG_WITH(gstreamer-exec-prefix,[  --with-gstreamer-exec-prefix=PFX Exec prefix where GStreamer is installed (optional)],
 		gstreamer_config_exec_prefix="$withval", gstreamer_config_exec_prefix="")
	AC_ARG_ENABLE(gstreamer-test, [  --disable-gstreamer-test	Do not try and run a test GStreamer-program],	
		, enable_gstreamer_test=yes)

	if test "x$gstreamer_config_prefix" != "x"; then
		gstreamer_config_args="$gstreamer_config_args --prefix=$gstreamer_config_prefix" ;

		if test "x${GSTREAMER_CONFIG+set}" != "xset" ; then
			GSTREAMER_CONFIG="$gstreamer_config_prefix/bin/gstreamer-config" 
		fi
	fi
	if test "x$gstreamer_config_exec_prefix" != "x"; then
		gstreamer_config_args="$gstreamer_config_args --exec-prefix=$gstreamer_config_exec_prefix" ;

		if test "x${GSTREAMER_CONFIG+set}" != "xset" ; then
			GSTREAMER_CONFIG="$gstreamer_config_exec_prefix/bin/gstreamer-config"
		fi	
	fi

	AC_PATH_PROG(GSTREAMER_CONFIG, gstreamer-config, no)

	if test "x$1" == "x" ; then
		min_gstreamer_version="0.0.1" 
	else
		min_gstreamer_version="$1"
	fi

dnl
dnl Check to make sure version wanted is better than the existing version
dnl
	AC_MSG_CHECKING(for GStreamer-version >= $min_gstreamer_version)
	no_gstreamer=""

	if test "x$GSTREAMER_CONFIG" = "xno" ; then
		no_gstreamer=yes
	else
		GSTREAMER_CFLAGS=`$GSTREAMER_CONFIG $gstreamer_config_args --cflags`
		GSTREAMER_LIBS=`$GSTREAMER_CONFIG $gstreamer_config_args --libs`

		gstreamer_config_major_version=`$GSTREAMER_CONFIG $gstreamer_config_args --version | \
						sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
		gstreamer_config_minor_version=`$GSTREAMER_CONFIG $gstreamer_config_args --version | \
						sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
		gstreamer_config_micro_version=`$GSTREAMER_CONFIG $gstreamer_config_args --version | \
						sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`

		if test "x$enable_gstreamer_test" = "xyes" ; then
			ac_save_CFLAGS="$CFLAGS"
			ac_save_LIBS="$LIBS"
			CFLAGS="$CFLAGS $GSTREAMER_CFLAGS"
			LIBS="$GSTREAMER_LIBS $LIBS"
dnl
dnl Try and run a program linked with libs
dnl
AC_TRY_RUN([
#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int 
main ()
{
	int major, minor, micro;
	char *tmp_version;

	system("touch conf.gstreamertest");

	tmp_version = strdup("$min_gstreamer_version");
	if (sscanf(tmp_version, "%d.%d.%d", &major, &minor, &micro) != 3) {
		printf("%s, bad version string\n", "$min_gstreamer_version");
		return 1;
	}	

	if (($gstreamer_config_major_version > major) ||
		(($gstreamer_config_major_version == major) && ($gstreamer_config_minor_version > minor)) ||
		(($gstreamer_config_major_version == major) && ($gstreamer_config_minor_version == minor) && 
		($gstreamer_config_micro_version >= micro))) {
		return 0;
	} else {
		printf("\n*** An old version of GStreamer(%d.%d.%d) was found.\n", 
			$gstreamer_config_major_version, $gstreamer_config_minor_version, 
			$gstreamer_config_micro_version);

		printf("*** You need a version of GStreamer newer than %d.%d.%d.\n", major, minor, micro);
	
	}

	return 1;
		
}
],, no_gstreamer=yes, [echo $ac_n "cross compiling; assuming OK... $ac_c"])
			CFLAGS="$ac_save_CFLAGS"
			LIBS="$ac_save_LIBS"
		fi
	fi
	if test "x$no_gstreamer" = "x" ; then
		AC_MSG_RESULT(yes)
		ifelse([$2], , :, [$2])
	else
dnl
dnl Something went wrong, looks like GStreamer was not found
dnl
		if test "$GSTREAMER_CONFIG" = "no" ; then
			echo "*** The gstreamer-config script installed by GStreamer could not be found"
			echo "*** If GStreamer was installed in PREFIX, make sure PREFIX/bin is in"
			echo "*** your path, or set the GSTREAMER_CONFIG environment variable to the"
			echo "*** full path to gstreamer-config."
		else
			if test -f conf.gstreamertest ; then
				:
			else
				echo "*** Could not run GStreamer test program, checking why..."
				CFLAGS="$CFLAGS $GSTREAMER_CFLAGS"
				LIBS="$LIBS $GSTREAMER_LIBS"
AC_TRY_LINK([
#include <gst/gst.h>
#include <stdio.h>
], 
[ 
gst_init(NULL, NULL); 
return 0; 
],
        [ echo "*** The test program compiled, but did not run. This usually means"
          echo "*** that the run-time linker is not finding GStreamer or finding the wrong"
          echo "*** version of GStreamer. If it is not finding GStreamer, you'll need to set your"
          echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
          echo "*** to the installed location  Also, make sure you have run ldconfig if that"
          echo "*** is required on your system"
          echo "***"
          echo "*** If you have an old version installed, it is best to remove it, although"
          echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH" ],
        [ echo "*** The test program failed to compile or link. See the file config.log for the"
          echo "*** exact error that occured. This usually means GStreamer was incorrectly installed"
          echo "*** or that you have moved GStreamer since it was installed. In the latter case, you"
          echo "*** may want to edit the gstremaer-config script: $GSTREAMER_CONFIG" ])
				CFLAGS="$ac_save_CFLAGS"
				LIBS="$ac_save_LIBS"
		       fi
		fi
		GSTREAMER_CFLAGS=""
		GSTREAMER_LIBS=""
		ifelse([$3], , :, [$3])
	fi

dnl
dnl Define our flags and libs
dnl
	AC_SUBST(GSTREAMER_CFLAGS)
	AC_SUBST(GSTREAMER_LIBS)
	rm -f conf.gstreamertest
])

