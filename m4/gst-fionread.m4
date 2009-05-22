AC_DEFUN([GST_CHECK_FIONREAD], [

  AC_MSG_CHECKING(for FIONREAD in sys/ioctl.h)
  AC_CACHE_VAL(_cv_gst_fionread_in_sys_ioctl, [
    AC_TRY_COMPILE([
#include <sys/types.h>
#include <sys/ioctl.h>
], [
int x = FIONREAD;
if ( x )
  return 0;
    ], _cv_gst_fionread_in_sys_ioctl="yes",_cv_gst_fionread_in_sys_ioctl="no")
  ])

  AC_MSG_RESULT($_cv_gst_fionread_in_sys_ioctl)

  if test "$_cv_gst_fionread_in_sys_ioctl" = "yes"; then
    AC_DEFINE([HAVE_FIONREAD_IN_SYS_IOCTL], 1, [FIONREAD ioctl found in sys/ioclt.h])

  else

    AC_MSG_CHECKING(for FIONREAD in sys/filio.h)
    AC_CACHE_VAL(_cv_gst_fionread_in_sys_filio, [
      AC_TRY_COMPILE([
  #include <sys/types.h>
  #include <sys/filio.h>
  ], [
  int x = FIONREAD;
  if ( x )
    return 0;
      ], _cv_gst_fionread_in_sys_filio="yes",_cv_gst_fionread_in_sys_filio="no")
    ])

    AC_MSG_RESULT($_cv_gst_fionread_in_sys_filio)

    if test "$_cv_gst_fionread_in_sys_filio" = "yes"; then
      AC_DEFINE([HAVE_FIONREAD_IN_SYS_FILIO], 1, [FIONREAD ioctl found in sys/filio.h])
    fi

  fi

])
