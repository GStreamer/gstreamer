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
AC_CHECK_LIB([asound], [snd_defaults_card],,
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


dnl Perform a check for a feature for GStreamer
dnl Richard Boulton <richard-alsa@tartarus.org>
dnl Last modification: 25/06/2001
dnl GST_CHECK_FEATURE(FEATURE-NAME, FEATURE-DESCRIPTION,
dnl                   DEPENDENT-PLUGINS, TEST-FOR-FEATURE)
dnl
dnl This macro adds a command line argument to enable the user to enable
dnl or disable a feature, and if the feature is enabled, performs a supplied
dnl test to check if the feature is available.
dnl
dnl The test should define HAVE_<FEATURE-NAME> to "yes" or "no" depending
dnl on whether the feature is available.
dnl 
dnl FEATURE-NAME        is the name of the feature, and should be in
dnl                     purely upper case characters.
dnl FEATURE-DESCRIPTION is used to describe the feature in help text for
dnl                     the command line argument.
dnl DEPENDENT-PLUGINS   lists any plugins which depend on this feature.
dnl TEST-FOR-FEATURE    is a test which sets HAVE_<FEATURE-NAME> to "yes"
dnl                     or "no" depending on whether the feature is
dnl                     available.
dnl
AC_DEFUN(GST_CHECK_FEATURE,
[
lower=translit([$1], A-Z, a-z)
AC_ARG_ENABLE(translit([$1], A-Z, a-z),
  [  ]--disable-translit([$1], A-Z, a-z)             enable [$2]: [$3],
  [ case "${enableval}" in
      yes) USE_[$1]=yes ;;
      no) USE_[$1]=no ;;
      *) AC_MSG_ERROR(bad value ${enableval} for --enable-${lower}) ;;
    esac],
  [ USE_$1=yes ])           dnl DEFAULT

dnl *** If it's enabled
if test x$USE_[$1] = xyes; then
  gst_check_save_LIBS=$LIBS
  gst_check_save_LDFLAGS=$LDFLAGS
  gst_check_save_CFLAGS=$CFLAGS
  gst_check_save_CPPFLAGS=$CPPFLAGS
  gst_check_save_CXXFLAGS=$CXXFLAGS
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
if test x$USE_[$1] = xno; then
  AC_MSG_WARN(
***** NOTE: These plugins won't be built: [$3]
)
fi
dnl *** Define the conditional as appropriate
AM_CONDITIONAL(USE_[$1], test x$USE_[$1] = xyes)
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

