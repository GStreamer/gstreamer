# a macro to get the libs/cflags for gscope
# serial 1

dnl AM_PATH_GSTREAMER([ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
dnl Test to see if timestamp is installed, and define GSTREAMER_CFLAGS, LIBS
dnl
AC_DEFUN(AM_PATH_GSTREAMER,
[dnl
dnl Get the cflags and libraries for the GtkScope widget
dnl
AC_ARG_WITH(gscope-prefix,
[  --with-gscope-prefix=PFX Prefix where GtkScope is installed],
GSTREAMER_PREFIX="$withval")

AC_CHECK_LIB(gscope,gtk_scope_new,
  AC_MSG_RESULT(yes),
  AC_MSG_RESULT(no),"$GSTREAMER_PREFIX $LIBS")
AC_SUBST(GSTREAMER_CFLAGS)
AC_SUBST(GSTREAMER_LIBS)
AC_SUBST(HAVE_GSTREAMER)
])
