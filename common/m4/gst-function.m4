dnl
dnl Check for compiler mechanism to show functions in debugging
dnl copied from an Ali patch floating on the internet
dnl
AC_DEFUN([AG_GST_CHECK_FUNCTION],[
  dnl #1: __PRETTY_FUNCTION__
  AC_MSG_CHECKING(whether $CC implements __PRETTY_FUNCTION__)
  AC_CACHE_VAL(have_pretty_function,[
    AC_TRY_LINK([#include <stdio.h>],
                [printf("%s", __PRETTY_FUNCTION__);],
                have_pretty_function=yes,
                have_pretty_function=no)
  ])
  AC_MSG_RESULT($have_pretty_function)
  if test "$have_pretty_function" = yes; then
    AC_DEFINE(HAVE_PRETTY_FUNCTION, 1,
              [defined if the compiler implements __PRETTY_FUNCTION__])
  fi

dnl #2: __FUNCTION__
  AC_MSG_CHECKING(whether $CC implements __FUNCTION__)
  AC_CACHE_VAL(have_function,[
    AC_TRY_LINK([#include <stdio.h>],
                [printf("%s", __FUNCTION__);],
                have_function=yes,
                have_function=no)
  ])
  AC_MSG_RESULT($have_function)
  if test "$have_function" = yes; then
    AC_DEFINE(HAVE_FUNCTION, 1,
              [defined if the compiler implements __FUNCTION__])
  fi

dnl #3: __func__
  AC_MSG_CHECKING(whether $CC implements __func__)
  AC_CACHE_VAL(have_func,[
    AC_TRY_LINK([#include <stdio.h>],
                [printf("%s", __func__);],
                have_func=yes,
                have_func=no)
  ])
  AC_MSG_RESULT($have_func)
  if test "$have_func" = yes; then
    AC_DEFINE(HAVE_FUNC, 1,
              [defined if the compiler implements __func__])
  fi

dnl now define FUNCTION to whatever works, and fallback to ""
  if test "$have_pretty_function" = yes; then
    function=__PRETTY_FUNCTION__
  else
    if test "$have_function" = yes; then
      function=__FUNCTION__
    else
      if test "$have_func" = yes; then
        function=__func__
      else
        function=\"\"
      fi
    fi
  fi
  AC_DEFINE_UNQUOTED(GST_FUNCTION, $function, [macro to use to show function name])
])
