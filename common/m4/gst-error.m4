dnl handle various error-related things

dnl Thomas Vander Stichele <thomas@apestaart.org>

dnl Last modification: 2005-10-16

dnl AG_GST_SET_ERROR_CFLAGS([ADD-WERROR])
dnl AG_GST_SET_LEVEL_DEFAULT([IS-CVS-VERSION])


dnl Sets ERROR_CFLAGS to something the compiler will accept.
dnl AC_SUBST them so they are available in Makefile

dnl -Wall is added if it is supported
dnl -Werror is added if ADD-WERROR is not "no"

dnl These flags can be overridden at make time:
dnl make ERROR_CFLAGS=
AC_DEFUN([AG_GST_SET_ERROR_CFLAGS],
[
  AC_REQUIRE([AC_PROG_CC])
  AC_REQUIRE([AS_COMPILER_FLAG])

  
  dnl if we support -Wall, set it unconditionally
  AS_COMPILER_FLAG(-Wall,
                   ERROR_CFLAGS="-Wall",
                   ERROR_CFLAGS="")
  
  dnl if asked for, add -Werror if supported
  if test "x$1" != "xno"
  then
    AS_COMPILER_FLAG(-Werror, ERROR_CFLAGS="$ERROR_CFLAGS -Werror")

    dnl if -Werror isn't suported
    if test "x$ERROR_CFLAGS" == "x"
    then
      dnl try -errwarn=%all,no%E_EMPTY_DECLARATION,no%E_STATEMENT_NOT_REACHED (Sun Forte case)
      dnl For Forte we need disable "empty declaration" warning produced by un-needed semicolon 
      dnl "statement not reached" disabled because there is g_assert_not_reached () in some places
      AS_COMPILER_FLAG([-errwarn=%all,no%E_EMPTY_DECLARATION,no%E_STATEMENT_NOT_REACHED],
                       [ERROR_CFLAGS="-errwarn=%all,no%E_EMPTY_DECLARATION,no%E_STATEMENT_NOT_REACHED"])

      dnl if this also isn't suported, try only for -errwarn=%all
      if test "x$ERROR_CFLAGS" == "x"
      then
        AS_COMPILER_FLAG(-errwarn=%all,
                         ERROR_CFLAGS="-errwarn=%all")
      fi
    fi
  fi

  AC_SUBST(ERROR_CFLAGS)
  AC_MSG_NOTICE([set ERROR_CFLAGS to $ERROR_CFLAGS])
])

dnl Sets the default error level for debugging messages
AC_DEFUN([AG_GST_SET_LEVEL_DEFAULT],
[
  dnl define correct errorlevel for debugging messages. We want to have
  dnl GST_ERROR messages printed when running cvs builds
  if test "x[$1]" = "xyes"; then
    GST_LEVEL_DEFAULT=GST_LEVEL_ERROR
  else
    GST_LEVEL_DEFAULT=GST_LEVEL_NONE
  fi
  AC_DEFINE_UNQUOTED(GST_LEVEL_DEFAULT, $GST_LEVEL_DEFAULT,
    [Default errorlevel to use])
  dnl AC_SUBST so we can use it for win32/common/config.h
  AC_SUBST(GST_LEVEL_DEFAULT)
])
