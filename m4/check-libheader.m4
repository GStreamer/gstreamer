dnl
dnl CHECK-LIBHEADER(FEATURE-NAME, LIB-NAME, LIB-FUNCTION, HEADER-NAME,
dnl                 ACTION-IF-FOUND, ACTION-IF-NOT-FOUND,
dnl                 EXTRA-LDFLAGS, EXTRA-CPPFLAGS)
dnl
dnl FEATURE-NAME        - feature name; library and header files are treated
dnl                       as feature, which we look for
dnl LIB-NAME            - library name as in AC_CHECK_LIB macro
dnl LIB-FUNCTION        - library symbol as in AC_CHECK_LIB macro
dnl HEADER-NAME         - header file name as in AC_CHECK_HEADER
dnl ACTION-IF-FOUND     - when feature is found then execute given action
dnl ACTION-IF-NOT-FOUND - when feature is not found then execute given action
dnl EXTRA-LDFLAGS       - extra linker flags (-L or -l)
dnl EXTRA-CPPFLAGS      - extra C preprocessor flags, i.e. -I/usr/X11R6/include
dnl
dnl Based on GST_CHECK_LIBHEADER from gstreamer plugins 0.3.1.
dnl
AC_DEFUN([CHECK_LIBHEADER],
[
  AC_CHECK_LIB([$2], [$3], HAVE_[$1]=yes, HAVE_[$1]=no, [$7])
  check_libheader_feature_name=translit([$1], A-Z, a-z)

  if test "x$HAVE_[$1]" = "xyes"; then
    check_libheader_save_CPPFLAGS=$CPPFLAGS
    CPPFLAGS="[$8] $CPPFLAGS"
    AC_CHECK_HEADER([$4], :, HAVE_[$1]=no)
    CPPFLAGS=$check_libheader_save_CPPFLAGS
  fi

  if test "x$HAVE_[$1]" = "xyes"; then
    ifelse([$5], , :, [$5])
    AC_MSG_NOTICE($check_libheader_feature_name was found)
  else
    ifelse([$6], , :, [$6])
    AC_MSG_WARN($check_libheader_feature_name not found)
  fi
  AC_SUBST(HAVE_[$1])
]
)
