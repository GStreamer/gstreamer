AC_DEFUN([AG_GST_DEBUGINFO], [
AC_ARG_ENABLE(debug,
AC_HELP_STRING([--disable-debug],[disable addition of -g debugging info]),
[case "${enableval}" in
  yes) USE_DEBUG=yes ;;
  no)  USE_DEBUG=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
esac],
[USE_DEBUG=yes]) dnl Default value

AC_ARG_ENABLE(DEBUG,
AC_HELP_STRING([--disable-DEBUG],[disables compilation of debugging messages]),
[case "${enableval}" in
  yes) ENABLE_DEBUG=yes ;;
  no)  ENABLE_DEBUG=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-DEBUG) ;;
esac],
[ENABLE_DEBUG=yes]) dnl Default value
if test x$ENABLE_DEBUG = xyes; then
  AC_DEFINE(GST_DEBUG_ENABLED, 1, [Define if DEBUG statements should be compiled in])
fi

AC_ARG_ENABLE(INFO,
AC_HELP_STRING([--disable-INFO],[disables compilation of informational messages]),
[case "${enableval}" in
  yes) ENABLE_INFO=yes ;;
  no)  ENABLE_INFO=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-INFO) ;;
esac],
[ENABLE_INFO=yes]) dnl Default value
if test x$ENABLE_INFO = xyes; then
  AC_DEFINE(GST_INFO_ENABLED, 1, [Define if INFO statements should be compiled in])
fi

AC_ARG_ENABLE(debug-color,
AC_HELP_STRING([--disable-debug-color],[disables color output of DEBUG and INFO output]),
[case "${enableval}" in
  yes) ENABLE_DEBUG_COLOR=yes ;;
  no)  ENABLE_DEBUG_COLOR=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug-color) ;;
esac],
[ENABLE_DEBUG_COLOR=yes]) dnl Default value
if test "x$ENABLE_DEBUG_COLOR" = xyes; then
  AC_DEFINE(GST_DEBUG_COLOR, 1, [Define if debugging messages should be colorized])
fi
])
