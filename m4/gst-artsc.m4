dnl Perform a check for existence of ARTSC
dnl Richard Boulton <richard-alsa@tartarus.org>
dnl Last modification: 26/06/2001
dnl GST_CHECK_ARTSC()
dnl
dnl This check was written for GStreamer: it should be renamed and checked
dnl for portability if you decide to use it elsewhere.
dnl
AC_DEFUN([GST_CHECK_ARTSC],
[ 
  AC_PATH_PROG(ARTSC_CONFIG, artsc-config, no)
  if test "x$ARTSC_CONFIG" = "xno"; then
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

