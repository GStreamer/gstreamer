# CFLAGS and library paths for FFMPEG
# taken from Autostar Sandbox, http://autostars.sourceforge.net/

dnl Usage:
dnl AM_PATH_FFMPEG([MINIMUM-VERSION, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl FIXME: version checking does not work currently
dnl
dnl Example:
dnl AM_PATH_FFMPEG(0.4.6, , AC_MSG_ERROR([*** FFMPEG >= 0.4.6 not installed)) 
dnl
dnl Defines FFMPEG_LIBS
dnl FIXME: should define FFMPEG_VERSION
dnl

AC_DEFUN([AM_PATH_FFMPEG],
[
  dnl allow for specification of a source path (for uninstalled)
  AC_ARG_WITH(ffmpeg-source,
    AC_HELP_STRING([--with-ffmpeg-source=DIR],
                   [Directory where FFmpeg source is (optional)]),
    ffmpeg_source="$withval")

  dnl save CFLAGS and LIBS here
  CFLAGS_save=$CFLAGS
  LIBS_save=$LIBS
  if test "x$ffmpeg_source" != "x"; then
    dnl uninstalled FFmpeg copy
    AC_MSG_NOTICE([Looking for FFmpeg source in $ffmpeg_source])
    CFLAGS="-I$ffmpeg_source/libav -I$ffmpeg_source/libavcodec"
    LIBS="-L$ffmpeg_source/libav -L$ffmpeg_source/libavcodec"
    AC_DEFINE_UNQUOTED(HAVE_FFMPEG_UNINSTALLED, 1,
                       [defined if we compile against uninstalled FFmpeg])
    FFMPEG_COMMON_INCLUDE="#include <common.h>"
  else
    FFMPEG_COMMON_INCLUDE="#include <ffmpeg/common.h>"
  fi
  
  dnl check for libavcodec
  AC_CHECK_LIB(avcodec, avcodec_init, HAVE_FFMPEG=yes, HAVE_FFMPEG=no)
  
  dnl check for avcodec.h and avformat.h
  if test "x$ffmpeg_source" != "x"; then
    dnl uninstalled
    AC_CHECK_HEADER(avcodec.h, , HAVE_FFMPEG=no, [/* only compile */])
    AC_CHECK_HEADER(avformat.h, , HAVE_FFMPEG=no, [/* only compile */])
  else
    AC_CHECK_HEADER(ffmpeg/avcodec.h, , HAVE_FFMPEG=no)
    AC_CHECK_HEADER(ffmpeg/avformat.h, , HAVE_FFMPEG=no)
  fi

dnl now check if it's sufficiently new

  AC_LANG_SAVE()
  AC_LANG_C()

  dnl FIXME: we use strcmp, which we know is going to break if ffmpeg ever uses
  dnl two digits for any of their version numbers.  It makes the test so much
  dnl easier though so let's ignore that
  AC_TRY_RUN([
$FFMPEG_COMMON_INCLUDE
#include <stdio.h>
#include <string.h>

int
main ()
{
  if (strcmp (FFMPEG_VERSION, "$1") == -1)
  {
    fprintf (stderr,
             "ERROR: your copy of ffmpeg is too old (%s)\n", FFMPEG_VERSION);
    return 1;
  }
  else
    return 0;
}
], , HAVE_FFMPEG=no)

dnl now do the actual "do we have it ?" test
  if test "x$HAVE_FFMPEG" = "xyes"; then
    FFMPEG_LIBS="$LIBS -lavcodec -lavformat"
    FFMPEG_CFLAGS="$CFLAGS"
    AC_MSG_NOTICE(we have ffmpeg)
    dnl execute what we have to because it's found
    ifelse([$2], , :, [$2])
  else
    FFMPEG_LIBS=""
    FFMPEG_CFLAGS=""
    dnl execute what we have to because it's not found
    ifelse([$3], , :, [$3])
  fi

dnl make variables available
  AC_SUBST(FFMPEG_LIBS)
  AC_SUBST(FFMPEG_CFLAGS)
  AC_SUBST(HAVE_FFMPEG)
  AC_LANG_RESTORE()
  CFLAGS=$CFLAGS_save
  LIBS=$LIBS_save
])
