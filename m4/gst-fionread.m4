AC_DEFUN([GST_CHECK_FIONREAD], [

  AC_MSG_CHECKING(for FIONREAD in sys/ioctl.h)
  AC_CACHE_VAL(GST_FIONREAD_IN_SYS_IOCTL, [
    AC_TRY_COMPILE([
#include <sys/types.h>
#include <sys/ioctl.h>
], [
int x = FIONREAD;
if ( x )
  return 0;
    ], GST_FIONREAD_IN_SYS_IOCTL="yes",GST_FIONREAD_IN_SYS_IOCTL="no")
  ])

  AC_MSG_RESULT($GST_FIONREAD_IN_SYS_IOCTL)

  if test "$GST_FIONREAD_IN_SYS_IOCTL" = "yes"; then
    AC_DEFINE([HAVE_FIONREAD_IN_SYS_IOCTL], 1, [FIONREAD ioctl found in sys/ioclt.h])

  else

    AC_MSG_CHECKING(for FIONREAD in sys/filio.h)
    AC_CACHE_VAL(GST_FIONREAD_IN_SYS_FILIO, [
      AC_TRY_COMPILE([
  #include <sys/types.h>
  #include <sys/filio.h>
  ], [
  int x = FIONREAD;
  if ( x )
    return 0;
      ], GST_FIONREAD_IN_SYS_FILIO="yes",GST_FIONREAD_IN_SYS_FILIO="no")
    ])

    AC_MSG_RESULT($GST_FIONREAD_IN_SYS_FILIO)

    if test "$GST_FIONREAD_IN_SYS_FILIO" = "yes"; then   
      AC_DEFINE([HAVE_FIONREAD_IN_SYS_FILIO], 1, [FIONREAD ioctl found in sys/filio.h])
    fi

  fi

])
