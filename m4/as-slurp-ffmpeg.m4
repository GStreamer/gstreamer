dnl slurp-ffmpeg.m4 0.1.1
dnl a macro to slurp in ffmpeg's cvs source inside a project tree
dnl taken from Autostar Sandbox, http://autostars.sourceforge.net/

dnl Usage:
dnl AS_SLURP_FFMPEG(DIRECTORY, DATE, [ACTION-IF-WORKED [, ACTION-IF-NOT-WORKED]]])
dnl
dnl Example:
dnl AM_PATH_FFMPEG(lib/ffmpeg, 2002-12-14 12:00 GMT)
dnl
dnl make sure you have a Tag file in the dir where you check out that
dnl is the Tag of CVS you want to have checked out
dnl it should correspond to the DATE argument you supply, ie resolve to
dnl the same date
dnl (in an ideal world, cvs would understand it's own Tag file format as
dnl a date spec)

AC_DEFUN([AS_SLURP_FFMPEG],
[
  # save original dir
  FAILED=""
  DIRECTORY=`pwd`
  # get/update cvs
  if test ! -d $1; then mkdir -p $1; fi
  dnl we need to check $srcdir/$1 or it will always checkout ffmpeg even if it is there
  dnl at least when top_srcdir != top_builddir.
  dnl FIXME: unfortunately this makes the checkout go into top_srcdir
  cd $srcdir/$1

  if test ! -e ffmpeg/README; then
    # check out cvs code
    AC_MSG_NOTICE(checking out ffmpeg cvs code from $2 into $1)
    cvs -Q -z4 -d:pserver:anonymous@mplayerhq.hu:/cvsroot/ffmpeg co -D '$2' ffmpeg || FAILED=yes
  else
    # compare against Tag file and see if it needs updating
    if test "`cat Tag`" == "$2"; then
      AC_MSG_NOTICE(ffmpeg cvs code in sync)
    else
      cd ffmpeg 
      AC_MSG_NOTICE(updating ffmpeg cvs code to $2)
      cvs -Q -z4 update -dP -D '$2' || FAILED=yes
      cd ..
    fi
  fi
  if test "x$FAILED" != "xyes"; then
    echo "$2" > Tag 
  fi
  
  # now go back
  cd $DIRECTORY

  if test "x$FAILED" == "xyes"; then
    [$4]
    false
  else
    [$3]
    true
  fi
])
