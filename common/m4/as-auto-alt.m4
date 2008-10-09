dnl as-auto-alt.m4 0.0.2
dnl autostars m4 macro for supplying alternate autotools versions to configure
dnl thomas@apestaart.org
dnl
dnl AS_AUTOTOOLS_ALTERNATE()
dnl
dnl supplies --with arguments for autoconf, autoheader, automake, aclocal

AC_DEFUN([AS_AUTOTOOLS_ALTERNATE],
[
  dnl allow for different autoconf version
  AC_ARG_WITH(autoconf,
    AC_HELP_STRING([--with-autoconf],
                   [use a different autoconf for regeneration of Makefiles]),
    [
      unset AUTOCONF
      AM_MISSING_PROG(AUTOCONF, ${withval})
      AC_MSG_NOTICE([Using $AUTOCONF as autoconf])
    ])

  dnl allow for different autoheader version
  AC_ARG_WITH(autoheader,
    AC_HELP_STRING([--with-autoheader],
                   [use a different autoheader for regeneration of Makefiles]),
    [
      unset AUTOHEADER
      AM_MISSING_PROG(AUTOHEADER, ${withval})
      AC_MSG_NOTICE([Using $AUTOHEADER as autoheader])
    ])

  dnl allow for different automake version
  AC_ARG_WITH(automake,
    AC_HELP_STRING([--with-automake],
                   [use a different automake for regeneration of Makefiles]),
    [
      unset AUTOMAKE
      AM_MISSING_PROG(AUTOMAKE, ${withval})
      AC_MSG_NOTICE([Using $AUTOMAKE as automake])
    ])

  dnl allow for different aclocal version
  AC_ARG_WITH(aclocal,
    AC_HELP_STRING([--with-aclocal],
                   [use a different aclocal for regeneration of Makefiles]),
    [
      unset ACLOCAL
      AM_MISSING_PROG(ACLOCAL, ${withval})
      AC_MSG_NOTICE([Using $ACLOCAL as aclocal])
    ])
])
