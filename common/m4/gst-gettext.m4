dnl gettext setup

dnl AG_GST_GETTEXT([gettext-package])
dnl defines GETTEXT_PACKAGE and LOCALEDIR

AC_DEFUN([AG_GST_GETTEXT],
[
  if test "$USE_NLS" = "yes"; then
    GETTEXT_PACKAGE=[$1]
  else
    GETTEXT_PACKAGE=[NULL]
  fi
  AC_SUBST(GETTEXT_PACKAGE)
  AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], "$GETTEXT_PACKAGE",
                     [gettext package name])

  dnl define LOCALEDIR in config.h
  AS_AC_EXPAND(LOCALEDIR, $datadir/locale)
  AC_DEFINE_UNQUOTED([LOCALEDIR], "$LOCALEDIR",
                     [gettext locale dir])
])
