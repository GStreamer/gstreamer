dnl check for a minimum version of GLib

dnl AG_GST_GLIB_CHECK([minimum-version-required])

AC_DEFUN([AG_GST_GLIB_CHECK],
[
  dnl Minimum required version of GLib
  GLIB_REQ=[$1]
  if test "x$GLIB_REQ" = "x"
  then
    AC_MSG_ERROR([Please specify a required version for GLib 2.0])
  fi
  AC_SUBST(GLIB_REQ)

  dnl Check for glib with everything
  PKG_CHECK_MODULES(GLIB,
    glib-2.0 >= $GLIB_REQ gobject-2.0 gthread-2.0 gmodule-no-export-2.0,
    HAVE_GLIB=yes,HAVE_GLIB=no)

  if test "x$HAVE_GLIB" = "xno"; then
    AC_MSG_ERROR([This package requires GLib >= $GLIB_REQ to compile.])
  fi

  dnl for the poor souls who for example have glib in /usr/local
  AS_SCRUB_INCLUDE(GLIB_CFLAGS)
])
