# CFLAGS and library paths for XMMS
# written 15 December 1999 by Ben Gertzfield <che@debian.org>

dnl Usage:
dnl AM_PATH_XMMS([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl
dnl Example:
dnl AM_PATH_XMMS(0.9.5.1, , AC_MSG_ERROR([*** XMMS >= 0.9.5.1 not installed - please install first ***]))
dnl
dnl Defines XMMS_CFLAGS, XMMS_LIBS, XMMS_DATA_DIR, XMMS_PLUGIN_DIR, 
dnl XMMS_VISUALIZATION_PLUGIN_DIR, XMMS_INPUT_PLUGIN_DIR, 
dnl XMMS_OUTPUT_PLUGIN_DIR, XMMS_GENERAL_PLUGIN_DIR, XMMS_EFFECT_PLUGIN_DIR,
dnl and XMMS_VERSION for your plugin pleasure.
dnl

dnl XMMS_TEST_VERSION(AVAILABLE-VERSION, NEEDED-VERSION [, ACTION-IF-OKAY [, ACTION-IF-NOT-OKAY]])
AC_DEFUN(XMMS_TEST_VERSION, [

# Determine which version number is greater. Prints 2 to stdout if	
# the second number is greater, 1 if the first number is greater,	
# 0 if the numbers are equal.						
									
# Written 15 December 1999 by Ben Gertzfield <che@debian.org>		
# Revised 15 December 1999 by Jim Monty <monty@primenet.com>		
									
    AC_PROG_AWK
    xmms_got_version=[` $AWK '						\
BEGIN {									\
    print vercmp(ARGV[1], ARGV[2]);					\
}									\
									\
function vercmp(ver1, ver2,    ver1arr, ver2arr,			\
                               ver1len, ver2len,			\
                               ver1int, ver2int, len, i, p) {		\
									\
    ver1len = split(ver1, ver1arr, /\./);				\
    ver2len = split(ver2, ver2arr, /\./);				\
									\
    len = ver1len > ver2len ? ver1len : ver2len;			\
									\
    for (i = 1; i <= len; i++) {					\
        p = 1000 ^ (len - i);						\
        ver1int += ver1arr[i] * p;					\
        ver2int += ver2arr[i] * p;					\
    }									\
									\
    if (ver1int < ver2int)						\
        return 2;							\
    else if (ver1int > ver2int)						\
        return 1;							\
    else								\
        return 0;							\
}' $1 $2`]								

    if test $xmms_got_version -eq 2; then 	# failure
	ifelse([$4], , :, $4)			
    else  					# success!
	ifelse([$3], , :, $3)
    fi
])

AC_DEFUN(AM_PATH_XMMS,
[
AC_ARG_WITH(xmms-prefix,[  --with-xmms-prefix=PFX  Prefix where XMMS is installed (optional)],
	xmms_config_prefix="$withval", xmms_config_prefix="")
AC_ARG_WITH(xmms-exec-prefix,[  --with-xmms-exec-prefix=PFX Exec prefix where XMMS is installed (optional)],
	xmms_config_exec_prefix="$withval", xmms_config_exec_prefix="")

if test x$xmms_config_exec_prefix != x; then
    xmms_config_args="$xmms_config_args --exec-prefix=$xmms_config_exec_prefix"
    if test x${XMMS_CONFIG+set} != xset; then
	XMMS_CONFIG=$xmms_config_exec_prefix/bin/xmms-config
    fi
fi

if test x$xmms_config_prefix != x; then
    xmms_config_args="$xmms_config_args --prefix=$xmms_config_prefix"
    if test x${XMMS_CONFIG+set} != xset; then
  	XMMS_CONFIG=$xmms_config_prefix/bin/xmms-config
    fi
fi

AC_PATH_PROG(XMMS_CONFIG, xmms-config, no)
min_xmms_version=ifelse([$1], ,0.9.5.1, $1)

if test "$XMMS_CONFIG" = "no"; then
    no_xmms=yes
else
    XMMS_CFLAGS=`$XMMS_CONFIG $xmms_config_args --cflags`
    XMMS_LIBS=`$XMMS_CONFIG $xmms_config_args --libs`
    XMMS_VERSION=`$XMMS_CONFIG $xmms_config_args --version`
    XMMS_DATA_DIR=`$XMMS_CONFIG $xmms_config_args --data-dir`
    XMMS_PLUGIN_DIR=`$XMMS_CONFIG $xmms_config_args --plugin-dir`
    XMMS_VISUALIZATION_PLUGIN_DIR=`$XMMS_CONFIG $xmms_config_args \
					--visualization-plugin-dir`
    XMMS_INPUT_PLUGIN_DIR=`$XMMS_CONFIG $xmms_config_args --input-plugin-dir`
    XMMS_OUTPUT_PLUGIN_DIR=`$XMMS_CONFIG $xmms_config_args --output-plugin-dir`
    XMMS_EFFECT_PLUGIN_DIR=`$XMMS_CONFIG $xmms_config_args --effect-plugin-dir`
    XMMS_GENERAL_PLUGIN_DIR=`$XMMS_CONFIG $xmms_config_args --general-plugin-dir`

    XMMS_TEST_VERSION($XMMS_VERSION, $min_xmms_version, ,no_xmms=version)
fi

AC_MSG_CHECKING(for XMMS - version >= $min_xmms_version)

if test "x$no_xmms" = x; then
    AC_MSG_RESULT(yes)
    ifelse([$2], , :, [$2])
else
    AC_MSG_RESULT(no)

    if test "$XMMS_CONFIG" = "no" ; then
	echo "*** The xmms-config script installed by XMMS could not be found."
      	echo "*** If XMMS was installed in PREFIX, make sure PREFIX/bin is in"
	echo "*** your path, or set the XMMS_CONFIG environment variable to the"
	echo "*** full path to xmms-config."
    else
	if test "$no_xmms" = "version"; then
	    echo "*** An old version of XMMS, $XMMS_VERSION, was found."
	    echo "*** You need a version of XMMS newer than $min_xmms_version."
	    echo "*** The latest version of XMMS is always available from"
	    echo "*** http://www.xmms.org/"
	    echo "***"

            echo "*** If you have already installed a sufficiently new version, this error"
            echo "*** probably means that the wrong copy of the xmms-config shell script is"
            echo "*** being found. The easiest way to fix this is to remove the old version"
            echo "*** of XMMS, but you can also set the XMMS_CONFIG environment to point to the"
            echo "*** correct copy of xmms-config. (In this case, you will have to"
            echo "*** modify your LD_LIBRARY_PATH enviroment variable, or edit /etc/ld.so.conf"
            echo "*** so that the correct libraries are found at run-time)"
	fi
    fi
    XMMS_CFLAGS=""
    XMMS_LIBS=""
    ifelse([$3], , :, [$3])
fi
AC_SUBST(XMMS_CFLAGS)
AC_SUBST(XMMS_LIBS)
AC_SUBST(XMMS_VERSION)
AC_SUBST(XMMS_DATA_DIR)
AC_SUBST(XMMS_PLUGIN_DIR)
AC_SUBST(XMMS_VISUALIZATION_PLUGIN_DIR)
AC_SUBST(XMMS_INPUT_PLUGIN_DIR)
AC_SUBST(XMMS_OUTPUT_PLUGIN_DIR)
AC_SUBST(XMMS_GENERAL_PLUGIN_DIR)
AC_SUBST(XMMS_EFFECT_PLUGIN_DIR)
])


dnl Perform a check for a feature for GStreamer
dnl Richard Boulton <richard-alsa@tartarus.org>
dnl Last modification: 25/06/2001
dnl GST_CHECK_FEATURE(FEATURE-NAME, FEATURE-DESCRIPTION,
dnl                   DEPENDENT-PLUGINS, TEST-FOR-FEATURE,
dnl                   DISABLE-BY-DEFAULT, ACTION-IF-USE, ACTION-IF-NOTUSE)
dnl
dnl This macro adds a command line argument to enable the user to enable
dnl or disable a feature, and if the feature is enabled, performs a supplied
dnl test to check if the feature is available.
dnl
dnl The test should define HAVE_<FEATURE-NAME> to "yes" or "no" depending
dnl on whether the feature is available.
dnl
dnl The macro will set USE_<<FEATURE-NAME> to "yes" or "no" depending on
dnl whether the feature is to be used.
dnl
dnl The macro will call AM_CONDITIONAL(USE_<<FEATURE-NAME>, ...) to allow
dnl the feature to control what is built in Makefile.ams.  If you want
dnl additional actions resulting from the test, you can add them with the
dnl ACTION-IF-USE and ACTION-IF-NOTUSE parameters.
dnl 
dnl FEATURE-NAME        is the name of the feature, and should be in
dnl                     purely upper case characters.
dnl FEATURE-DESCRIPTION is used to describe the feature in help text for
dnl                     the command line argument.
dnl DEPENDENT-PLUGINS   lists any plugins which depend on this feature.
dnl TEST-FOR-FEATURE    is a test which sets HAVE_<FEATURE-NAME> to "yes"
dnl                     or "no" depending on whether the feature is
dnl                     available.
dnl DISABLE-BY-DEFAULT  if "disabled", the feature is disabled by default,
dnl                     if any other value, the feature is enabled by default.
dnl ACTION-IF-USE       any extra actions to perform if the feature is to be
dnl                     used.
dnl ACTION-IF-NOTUSE    any extra actions to perform if the feature is not to
dnl                     be used.
dnl
AC_DEFUN(GST_CHECK_FEATURE,
[dnl
builtin(define, [gst_endisable], ifelse($5, [disabled], [enable], [disable]))dnl
AC_ARG_ENABLE(translit([$1], A-Z, a-z),
  [  ]builtin(format, --%-26s gst_endisable %s, gst_endisable-translit([$1], A-Z, a-z), [$2]ifelse([$3],,,: [$3])),
  [ case "${enableval}" in
      yes) USE_[$1]=yes ;;
      no) USE_[$1]=no ;;
      *) AC_MSG_ERROR(bad value ${enableval} for --enable-translit([$1], A-Z, a-z)) ;;
    esac],
  [ USE_$1=]ifelse($5, [disabled], [no], [yes]))           dnl DEFAULT

dnl *** If it's enabled
if test x$USE_[$1] = xyes; then
  gst_check_save_LIBS=$LIBS
  gst_check_save_LDFLAGS=$LDFLAGS
  gst_check_save_CFLAGS=$CFLAGS
  gst_check_save_CPPFLAGS=$CPPFLAGS
  gst_check_save_CXXFLAGS=$CXXFLAGS
  HAVE_[$1]=no
  $4
  LIBS=$gst_check_save_LIBS
  LDFLAGS=$gst_check_save_LDFLAGS
  CFLAGS=$gst_check_save_CFLAGS
  CPPFLAGS=$gst_check_save_CPPFLAGS
  CXXFLAGS=$gst_check_save_CXXFLAGS

  dnl If it isn't found, unset USE_[$1]
  if test x$HAVE_[$1] = xno; then
    USE_[$1]=no
  fi
fi
dnl *** Warn if it's disabled or not found
if test x$USE_[$1] = xyes; then
  ifelse([$6], , :, [$6])
else
  ifelse([$3], , :, [AC_MSG_WARN(
***** NOTE: These plugins won't be built: [$3]
)])
  ifelse([$7], , :, [$7])
fi
dnl *** Define the conditional as appropriate
AM_CONDITIONAL(USE_[$1], test x$USE_[$1] = xyes)
])

dnl Use a -config program which accepts --cflags and --libs parameters
dnl to set *_CFLAGS and *_LIBS and check existence of a feature.
dnl Richard Boulton <richard-alsa@tartarus.org>
dnl Last modification: 26/06/2001
dnl GST_CHECK_CONFIGPROG(FEATURE-NAME, CONFIG-PROG-FILENAME, MODULES)
dnl
dnl This check was written for GStreamer: it should be renamed and checked
dnl for portability if you decide to use it elsewhere.
dnl
AC_DEFUN(GST_CHECK_CONFIGPROG,
[
  AC_PATH_PROG([$1]_CONFIG, [$2], no)
  if test x$[$1]_CONFIG = xno; then
    [$1]_LIBS=
    [$1]_CFLAGS=
    HAVE_[$1]=no
  else
    [$1]_LIBS=`[$2] --libs [$3]`
    [$1]_CFLAGS=`[$2] --cflags [$3]`
    HAVE_[$1]=yes
  fi
  AC_SUBST([$1]_LIBS)
  AC_SUBST([$1]_CFLAGS)
])


dnl Perform a check for existence of ARTSC
dnl Richard Boulton <richard-alsa@tartarus.org>
dnl Last modification: 26/06/2001
dnl GST_CHECK_FEATURE(FEATURE-NAME, FEATURE-DESCRIPTION,
dnl                   DEPENDENT-PLUGINS, TEST-FOR-FEATURE)
dnl
dnl This check was written for GStreamer: it should be renamed and checked
dnl for portability if you decide to use it elsewhere.
dnl
AC_DEFUN(GST_CHECK_ARTSC,
[
  AC_PATH_PROG(ARTSC_CONFIG, artsc-config, no)
  if test x$ARTSC_CONFIG = xno; then
    AC_MSG_WARN([Couldn't find artsc-config])
    HAVE_ARTSC=no
    ARTSC_LIBS=
    ARTSC_CFLAGS=
  else
    ARTSC_LIBS=`artsc-config --libs`
    ARTSC_CFLAGS=`artsc-config --cflags`
    dnl AC_CHECK_HEADER uses CPPFLAGS, but not CFLAGS.
    dnl FIXME: Ensure only suitable flags result from artsc-config --cflags
    CPPFLAGS="$CPPFLAGS $ARTSC_CFLAGS"
    AC_CHECK_HEADER(artsc.h, HAVE_ARTSC=yes, HAVE_ARTSC=no)
  fi
  AC_SUBST(ARTSC_LIBS)
  AC_SUBST(ARTSC_CFLAGS)
])

dnl sidplay stuff (taken from xmms)
AC_DEFUN(AC_FIND_FILE,
[
  $3=NO
  for i in $2; do
    for j in $1; do
      if test -r "$i/$j"; then
        $3=$i
        break 2
      fi
    done
  done
])

AC_DEFUN(AC_PATH_LIBSIDPLAY,
[
AC_MSG_CHECKING([for SIDPLAY includes and library])
ac_sidplay_cflags=NO
ac_sidplay_library=NO
sidplay_cflags=""
sidplay_library=""

AC_ARG_WITH(sidplay-includes,
  [  --with-sidplay-includes=DIR
                          where the sidplay includes are located],
  [ac_sidplay_cflags="$withval"
  ])

AC_ARG_WITH(sidplay-library,
  [  --with-sidplay-library=DIR
                          where the sidplay library is installed],
  [ac_sidplay_library="$withval"
  ])

if test "$ac_sidplay_cflags" = NO || test "$ac_sidplay_library" = NO; then
#search common locations

AC_CACHE_VAL(ac_cv_have_sidplay,
[
sidplay_incdirs="$ac_sidplay_cflags /usr/include /usr/local/include /usr/lib/sidplay/include /usr/local/lib/sidplay/include"
AC_FIND_FILE(sidplay/sidtune.h,$sidplay_incdirs,sidplay_foundincdir)
ac_sidplay_cflags=$sidplay_foundincdir

sidplay_libdirs="$ac_sidplay_library /usr/lib /usr/local/lib /usr/lib/sidplay /usr/local/lib/sidplay"
AC_FIND_FILE(libsidplay.so libsidplay.so.1 libsidplay.so.1.36 libsidplay.so.1.37,$sidplay_libdirs,sidplay_foundlibdir)
ac_sidplay_library=$sidplay_foundlibdir

if test "$ac_sidplay_cflags" = NO || test "$ac_sidplay_library" = NO; then
  ac_cv_have_sidplay="have_sidplay=no"
  ac_sidplay_notfound=""
  if test "$ac_sidplay_cflags" = NO; then
    if test "$ac_sidplay_library" = NO; then
      ac_sidplay_notfound="(headers and library)";
    else
      ac_sidplay_notfound="(headers)";
    fi
  else
    ac_sidplay_notfound="(library)";
  fi
  eval "$ac_cv_have_sidplay"
  AC_MSG_RESULT([$have_sidplay])
else
  have_sidplay=yes
fi

])  dnl AC_CACHE_VAL(ac_cv_have_sidplay,
else
  have_sidplay=yes
fi  dnl if (have_to_search)

eval "$ac_cv_have_sidplay"

if test "$have_sidplay" != yes; then
  AC_MSG_RESULT([$have_sidplay]);
else
  ac_cv_have_sidplay="have_sidplay=yes \
    ac_sidplay_cflags=$ac_sidplay_cflags ac_sidplay_library=$ac_sidplay_library"
  AC_MSG_RESULT([library $ac_sidplay_library, headers $ac_sidplay_cflags])
  
  sidplay_library=$ac_sidplay_library
  sidplay_cflags=$ac_sidplay_cflags
  
  SIDPLAY_LIBS="-L$sidplay_library -lsidplay"
  all_libraries="$SIDPLAY_LIBS $all_libraries"
  SIDPLAY_CFLAGS="-I$sidplay_cflags"
  all_includes="$SIDPLAY_CFLAGS $all_includes"
fi

dnl Test compilation.

AC_MSG_CHECKING([whether -lsidplay works])
ac_cxxflags_safe=$CXXFLAGS
ac_ldflags_safe=$LDFLAGS
ac_libs_safe=$LIBS

CXXFLAGS="$CXXFLAGS -I$sidplay_cflags"
LDFLAGS="$LDFLAGS -L$sidplay_library"
LIBS="-lsidplay"

AC_CACHE_VAL(ac_cv_sidplay_works,
[
  AC_LANG_CPLUSPLUS
  AC_TRY_RUN([
    #include <sidplay/player.h>
	     
    int main()
    {
      sidTune tune = sidTune(0);
    }
    ],
    ac_cv_sidplay_works="yes",
    ac_cv_sidplay_works="no",
    ac_cv_sidplay_works="no")
  AC_LANG_C
])

CXXFLAGS="$ac_cxxflags_safe"
LDFLAGS="$ac_ldflags_safe"
LIBS="$ac_libs_safe"

AC_MSG_RESULT([$ac_cv_sidplay_works])
if test "$ac_cv_sidplay_works" != yes; then
  have_sidplay=no
fi

dnl

AC_SUBST(SIDPLAY_CFLAGS)
AC_SUBST(SIDPLAY_LIBS)

AC_SUBST(sidplay_library)
AC_SUBST(sidplay_cflags)

])
dnl Configure Paths for Alsa
dnl Some modifications by Richard Boulton <richard-alsa@tartarus.org>
dnl Christopher Lansdown <lansdoct@cs.alfred.edu>
dnl Jaroslav Kysela <perex@suse.cz>
dnl Last modification: 07/01/2001
dnl AM_PATH_ALSA([MINIMUM-VERSION [, ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl Test for libasound, and define ALSA_CFLAGS and ALSA_LIBS as appropriate.
dnl enables arguments --with-alsa-prefix=
dnl                   --with-alsa-enc-prefix=
dnl                   --disable-alsatest  (this has no effect, as yet)
dnl
dnl For backwards compatibility, if ACTION_IF_NOT_FOUND is not specified,
dnl and the alsa libraries are not found, a fatal AC_MSG_ERROR() will result.
dnl
AC_DEFUN(AM_PATH_ALSA,
[dnl Save the original CFLAGS, LDFLAGS, and LIBS
alsa_save_CFLAGS="$CFLAGS"
alsa_save_LDFLAGS="$LDFLAGS"
alsa_save_LIBS="$LIBS"
alsa_found=yes

dnl
dnl Get the cflags and libraries for alsa
dnl
AC_ARG_WITH(alsa-prefix,
[  --with-alsa-prefix=PFX  Prefix where Alsa library is installed(optional)],
[alsa_prefix="$withval"], [alsa_prefix=""])

AC_ARG_WITH(alsa-inc-prefix,
[  --with-alsa-inc-prefix=PFX  Prefix where include libraries are (optional)],
[alsa_inc_prefix="$withval"], [alsa_inc_prefix=""])

dnl FIXME: this is not yet implemented
AC_ARG_ENABLE(alsatest,
[  --disable-alsatest      Do not try to compile and run a test Alsa program],
[enable_alsatest=no],
[enable_alsatest=yes])

dnl Add any special include directories
AC_MSG_CHECKING(for ALSA CFLAGS)
if test "$alsa_inc_prefix" != "" ; then
	ALSA_CFLAGS="$ALSA_CFLAGS -I$alsa_inc_prefix"
	CFLAGS="$CFLAGS -I$alsa_inc_prefix"
fi
AC_MSG_RESULT($ALSA_CFLAGS)

dnl add any special lib dirs
AC_MSG_CHECKING(for ALSA LDFLAGS)
if test "$alsa_prefix" != "" ; then
	ALSA_LIBS="$ALSA_LIBS -L$alsa_prefix"
	LDFLAGS="$LDFLAGS $ALSA_LIBS"
fi

dnl add the alsa library
ALSA_LIBS="$ALSA_LIBS -lasound -lm -ldl"
LIBS=`echo $LIBS | sed 's/-lm//'`
LIBS=`echo $LIBS | sed 's/-ldl//'`
LIBS=`echo $LIBS | sed 's/  //'`
LIBS="$ALSA_LIBS $LIBS"
AC_MSG_RESULT($ALSA_LIBS)

dnl Check for a working version of libasound that is of the right version.
min_alsa_version=ifelse([$1], ,0.1.1,$1)
AC_MSG_CHECKING(for libasound headers version >= $min_alsa_version)
no_alsa=""
    alsa_min_major_version=`echo $min_alsa_version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
    alsa_min_minor_version=`echo $min_alsa_version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
    alsa_min_micro_version=`echo $min_alsa_version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`

AC_LANG_SAVE
AC_LANG_C
AC_TRY_COMPILE([
#include <sys/asoundlib.h>
], [
void main(void)
{
/* ensure backward compatibility */
#if !defined(SND_LIB_MAJOR) && defined(SOUNDLIB_VERSION_MAJOR)
#define SND_LIB_MAJOR SOUNDLIB_VERSION_MAJOR
#endif
#if !defined(SND_LIB_MINOR) && defined(SOUNDLIB_VERSION_MINOR)
#define SND_LIB_MINOR SOUNDLIB_VERSION_MINOR
#endif
#if !defined(SND_LIB_SUBMINOR) && defined(SOUNDLIB_VERSION_SUBMINOR)
#define SND_LIB_SUBMINOR SOUNDLIB_VERSION_SUBMINOR
#endif

#  if(SND_LIB_MAJOR > $alsa_min_major_version)
  exit(0);
#  else
#    if(SND_LIB_MAJOR < $alsa_min_major_version)
#       error not present
#    endif

#   if(SND_LIB_MINOR > $alsa_min_minor_version)
  exit(0);
#   else
#     if(SND_LIB_MINOR < $alsa_min_minor_version)
#          error not present
#      endif

#      if(SND_LIB_SUBMINOR < $alsa_min_micro_version)
#        error not present
#      endif
#    endif
#  endif
exit(0);
}
],
  [AC_MSG_RESULT(found.)],
  [AC_MSG_RESULT(not present.)
   ifelse([$3], , [AC_MSG_ERROR(Sufficiently new version of libasound not found.)])
   alsa_found=no]
)
AC_LANG_RESTORE

dnl Now that we know that we have the right version, let's see if we have the library and not just the headers.
AC_CHECK_LIB([asound], [snd_seq_create_event],,
	[ifelse([$3], , [AC_MSG_ERROR(No linkable libasound was found.)])
	 alsa_found=no]
)

if test "x$alsa_found" = "xyes" ; then
   ifelse([$2], , :, [$2])
   LIBS=`echo $LIBS | sed 's/-lasound//g'`
   LIBS=`echo $LIBS | sed 's/  //'`
   LIBS="-lasound $LIBS"
fi
if test "x$alsa_found" = "xno" ; then
   ifelse([$3], , :, [$3])
   CFLAGS="$alsa_save_CFLAGS"
   LDFLAGS="$alsa_save_LDFLAGS"
   LIBS="$alsa_save_LIBS"
   ALSA_CFLAGS=""
   ALSA_LIBS=""
fi

dnl That should be it.  Now just export out symbols:
AC_SUBST(ALSA_CFLAGS)
AC_SUBST(ALSA_LIBS)
])

# stuff for SDL, hope this helps if we put it here

# Configure paths for SDL
# Sam Lantinga 9/21/99
# stolen from Manish Singh
# stolen back from Frank Belew
# stolen from Manish Singh
# Shamelessly stolen from Owen Taylor

dnl AM_PATH_SDL([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl Test for SDL, and define SDL_CFLAGS and SDL_LIBS
dnl
AC_DEFUN(AM_PATH_SDL,
[dnl 
dnl Get the cflags and libraries from the sdl-config script
dnl
AC_ARG_WITH(sdl-prefix,[  --with-sdl-prefix=PFX   Prefix where SDL is installed (optional)],
            sdl_prefix="$withval", sdl_prefix="")
AC_ARG_WITH(sdl-exec-prefix,[  --with-sdl-exec-prefix=PFX Exec prefix where SDL is installed (optional)],
            sdl_exec_prefix="$withval", sdl_exec_prefix="")
AC_ARG_ENABLE(sdltest, [  --disable-sdltest       Do not try to compile and run a test SDL program],
		    , enable_sdltest=yes)

  if test x$sdl_exec_prefix != x ; then
     sdl_args="$sdl_args --exec-prefix=$sdl_exec_prefix"
     if test x${SDL_CONFIG+set} != xset ; then
        SDL_CONFIG=$sdl_exec_prefix/bin/sdl-config
     fi
  fi
  if test x$sdl_prefix != x ; then
     sdl_args="$sdl_args --prefix=$sdl_prefix"
     if test x${SDL_CONFIG+set} != xset ; then
        SDL_CONFIG=$sdl_prefix/bin/sdl-config
     fi
  fi

  AC_PATH_PROG(SDL_CONFIG, sdl-config, no)
  min_sdl_version=ifelse([$1], ,0.11.0,$1)
  AC_MSG_CHECKING(for SDL - version >= $min_sdl_version)
  no_sdl=""
  if test "$SDL_CONFIG" = "no" ; then
    no_sdl=yes
  else
    SDL_CFLAGS=`$SDL_CONFIG $sdlconf_args --cflags`
    SDL_LIBS=`$SDL_CONFIG $sdlconf_args --libs`

    sdl_major_version=`$SDL_CONFIG $sdl_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
    sdl_minor_version=`$SDL_CONFIG $sdl_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
    sdl_micro_version=`$SDL_CONFIG $sdl_config_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`
    if test "x$enable_sdltest" = "xyes" ; then
      ac_save_CFLAGS="$CFLAGS"
      ac_save_LIBS="$LIBS"
      CFLAGS="$CFLAGS $SDL_CFLAGS"
      LIBS="$LIBS $SDL_LIBS"
dnl
dnl Now check if the installed SDL is sufficiently new. (Also sanity
dnl checks the results of sdl-config to some extent
dnl
      rm -f conf.sdltest
      AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL.h"

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
  system ("touch conf.sdltest");
  */
  { FILE *fp = fopen("conf.sdltest", "a"); if ( fp ) fclose(fp); }

  /* HP/UX 9 (%@#!) writes to sscanf strings */
  tmp_version = my_strdup("$min_sdl_version");
  if (sscanf(tmp_version, "%d.%d.%d", &major, &minor, &micro) != 3) {
     printf("%s, bad version string\n", "$min_sdl_version");
     exit(1);
   }

   if (($sdl_major_version > major) ||
      (($sdl_major_version == major) && ($sdl_minor_version > minor)) ||
      (($sdl_major_version == major) && ($sdl_minor_version == minor) && ($sdl_micro_version >= micro)))
    {
      return 0;
    }
  else
    {
      printf("\n*** 'sdl-config --version' returned %d.%d.%d, but the minimum version\n", $sdl_major_version, $sdl_minor_version, $sdl_micro_version);
      printf("*** of SDL required is %d.%d.%d. If sdl-config is correct, then it is\n", major, minor, micro);
      printf("*** best to upgrade to the required version.\n");
      printf("*** If sdl-config was wrong, set the environment variable SDL_CONFIG\n");
      printf("*** to point to the correct copy of sdl-config, and remove the file\n");
      printf("*** config.cache before re-running configure\n");
      return 1;
    }
}

],, no_sdl=yes,[echo $ac_n "cross compiling; assumed OK... $ac_c"])
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
     fi
  fi
  if test "x$no_sdl" = x ; then
     AC_MSG_RESULT(yes)
     ifelse([$2], , :, [$2])     
  else
     AC_MSG_RESULT(no)
     if test "$SDL_CONFIG" = "no" ; then
       echo "*** The sdl-config script installed by SDL could not be found"
       echo "*** If SDL was installed in PREFIX, make sure PREFIX/bin is in"
       echo "*** your path, or set the SDL_CONFIG environment variable to the"
       echo "*** full path to sdl-config."
     else
       if test -f conf.sdltest ; then
        :
       else
          echo "*** Could not run SDL test program, checking why..."
          CFLAGS="$CFLAGS $SDL_CFLAGS"
          LIBS="$LIBS $SDL_LIBS"
          AC_TRY_LINK([
#include <stdio.h>
#include "SDL.h"

int main(int argc, char *argv[])
{ return 0; }
#undef  main
#define main K_and_R_C_main
],      [ return 0; ],
        [ echo "*** The test program compiled, but did not run. This usually means"
          echo "*** that the run-time linker is not finding SDL or finding the wrong"
          echo "*** version of SDL. If it is not finding SDL, you'll need to set your"
          echo "*** LD_LIBRARY_PATH environment variable, or edit /etc/ld.so.conf to point"
          echo "*** to the installed location  Also, make sure you have run ldconfig if that"
          echo "*** is required on your system"
	  echo "***"
          echo "*** If you have an old version installed, it is best to remove it, although"
          echo "*** you may also be able to get things to work by modifying LD_LIBRARY_PATH"],
        [ echo "*** The test program failed to compile or link. See the file config.log for the"
          echo "*** exact error that occured. This usually means SDL was incorrectly installed"
          echo "*** or that you have moved SDL since it was installed. In the latter case, you"
          echo "*** may want to edit the sdl-config script: $SDL_CONFIG" ])
          CFLAGS="$ac_save_CFLAGS"
          LIBS="$ac_save_LIBS"
       fi
     fi
     SDL_CFLAGS=""
     SDL_LIBS=""
     ifelse([$3], , :, [$3])
  fi
  AC_SUBST(SDL_CFLAGS)
  AC_SUBST(SDL_LIBS)
  rm -f conf.sdltest
])
# Configure paths for ARTS
# Philip Stadermann   2001-06-21
# stolen from esd.m4
# adapted and scrubbed by thomas

dnl AM_PATH_ARTS([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl Test for ARTS, and define ARTS_CFLAGS and ARTS_LIBS
dnl
AC_DEFUN([AM_PATH_ARTS],
[dnl 
dnl Get the cflags and libraries from the artsc-config script
dnl
AC_ARG_WITH(arts-prefix,[  --with-arts-prefix=PFX  Prefix where ARTS is installed (optional)],
            arts_prefix="$withval", arts_prefix="")
AC_ARG_WITH(arts-exec-prefix,[  --with-arts-exec-prefix=PFX                                                                             Exec prefix where ARTS is installed (optional)],
            arts_exec_prefix="$withval", arts_exec_prefix="")
AC_ARG_ENABLE(artstest, [  --disable-artstest      Do not try to compile and run a test ARTS program],
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
    # FIXME : thomas added this sed to get arts path instead of artsc
    ARTS_CFLAGS=`$ARTS_CONFIG $artsconf_args --cflags | sed 's/artsc$/arts/'`
    ARTS_LIBS=`$ARTS_CONFIG $artsconf_args --libs | sed 's/artsc$/arts/'`

    arts_major_version=`$ARTS_CONFIG $arts_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\1/'`
    arts_minor_version=`$ARTS_CONFIG $arts_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\2/'`
    arts_micro_version=`$ARTS_CONFIG $arts_config_args --version | \
           sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[0-9]]*\)/\3/'`
    if test "x$enable_artstest" = "xyes" ; then
      ac_save_CFLAGS="$CFLAGS"
      ac_save_LIBS="$LIBS"
      CFLAGS="$CFLAGS $ARTS_CFLAGS"
      LIBS="$LIBS $ARTS_LIBS"
dnl
dnl Now check if the installed ARTS is sufficiently new. (Also sanity
dnl checks the results of artsc-config to some extent)
dnl
dnl thomas: ask nicely for C++ compilation
AC_LANG_PUSH(C++)
AC_SUBST(CPPFLAGS,"$CPPFLAGS $ARTS_CFLAGS")
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
       CFLAGS="$ac_save_CFLAGS"
       LIBS="$ac_save_LIBS"
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
          CFLAGS="$CFLAGS $ARTS_CFLAGS"
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
     ARTS_CFLAGS=""
     ARTS_LIBS=""
     ifelse([$3], , :, [$3])
  fi
  AC_SUBST(ARTS_CFLAGS)
  AC_SUBST(ARTS_LIBS)
  rm -f conf.artstest
])
dnl release C++ question

