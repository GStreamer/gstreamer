dnl Configure Paths for Alsa
dnl Christopher Lansdown (lansdoct@cs.alfred.edu)
dnl 29/10/1998
dnl AM_PATH_ALSA(MINIMUM-VERSION)
dnl Test for libasound, and define ALSA_CFLAGS and ALSA_LIBS as appropriate.
dnl enables arguments --with-alsa-prefix= --with-alsa-enc-prefix= --disable-alsatest
dnl
AC_DEFUN(AM_PATH_ALSA,
[dnl
dnl Get the clfags and libraries for alsa
dnl
AC_ARG_WITH(alsa-prefix,[ --with-alsa-prefix=PFX  Prefix where Alsa library is installed(optional)],
	[alsa_prefix="$withval"], [alsa_prefix=""])
AC_ARG_WITH(alsa-inc-prefix, [ --with-alsa-inc-prefix=PFX  Prefix where include libraries are (optional)],
	[alsa_inc_prefix="$withval"], [alsa_inc_prefix=""])
AC_ARG_ENABLE(alsatest, [ --disable-alsatest       Do not try to compile and run a test Alsa program], [enable_alsatest=no], [enable_alsatest=yes])

dnl Add any special include directories
AC_MSG_CHECKING(for ALSA CFLAGS)
if test "$alsa_inc_prefix" != "" ; then
	ALSA_CFLAGS="$ALSA_CFLAGS -I$alsa_inc_prefix"
	CFLAGS="-I$alsa_inc_prefix"
fi
AC_MSG_RESULT($ALSA_CFLAGS)

dnl add any special lib dirs
AC_MSG_CHECKING(for ALSA LDFLAGS)
if test "$alsa_prefix" != "" ; then
	ALSA_LIBS="$ALSA_LIBS -L$alsa_prefix"
	LIBS="-L$alsa_prefix"
fi

dnl add the alsa library
ALSA_LIBS="$ALSA_LIBS -lasound"
LDFLAGS="$ALSA_LIBS -lasound"
AC_MSG_RESULT($ALSA_LIBS)

dnl Check for the presence of the library
dnl if test $enable_alsatest = yes; then
dnl   AC_MSG_CHECKING(for working libasound)
dnl   AC_TRY_RUN([
dnl #include <sys/asoundlib.h>
dnl void main(void)
dnl {
dnl   snd_cards();
dnl   exit(0);
dnl }
dnl ],
dnl    [AC_MSG_RESULT("present")],
dnl    [AC_MSG_RESULT("not found. ")
dnl    AC_MSG_ERROR(Fatal error: Install alsa-lib package or use --with-alsa-prefix option...)],
dnl    [AC_MSG_RESULT(unsopported)
dnl     AC_MSG_ERROR(Cross-compiling isn't supported...)]
dnl  )
dnl fi

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
   AC_MSG_WARN(Sufficiently new version of libasound not found.)]
)
AC_LANG_RESTORE

dnl Now that we know that we have the right version, let's see if we have the library and not just the headers.
AC_CHECK_LIB([asound], [snd_cards],,
	[AC_MSG_WARN(No linkable libasound was found.)]
)

dnl That should be it.  Now just export out symbols:
AC_SUBST(ALSA_CFLAGS)
AC_SUBST(ALSA_LIBS)
])


