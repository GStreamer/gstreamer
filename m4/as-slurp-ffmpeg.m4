dnl slurp-ffmpeg.m4 0.1.0
dnl a macro to slurp in ffmpeg's cvs source inside a project tree
dnl taken from Autostar Sandbox, http://autostars.sourceforge.net/

dnl Usage:
dnl AS_SLURP_FFMPEG(DIRECTORY, DATE, [ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
dnl
dnl Example:
dnl AM_PATH_FFMPEG(lib/ffmpeg, "2002-12-14 12:00 GMT")
nl

AC_DEFUN(AS_SLURP_FFMPEG,
[
  # save original dir
  DIRECTORY=`pwd`
  # get/update cvs
  if test ! -d $1; then mkdir -p $1; fi
  cd $1

  if test ! -d ffmpeg/CVS; then
    # check out cvs code
    AC_MSG_NOTICE(checking out ffmpeg cvs code from $2 into $1)
    cvs -Q -d:pserver:anonymous@cvs.ffmpeg.sourceforge.net:/cvsroot/ffmpeg co -D $2 ffmpeg || FAILED=yes
    cd ffmpeg
  else
    cd ffmpeg 
    AC_MSG_NOTICE(updating ffmpeg cvs code)
    cvs -Q update -dP -D $2 || FAILED=yes
  fi
  
  # now configure it
    AC_MSG_NOTICE(configuring ffmpeg cvs code)
  ./configure

  # now go back
  cd $DIRECTORY

  if test "x$FAILED" == "xyes"; then
    [$4]
  else
    [$3]
  fi
])
