dnl
dnl MPEG2DEC_CHECK-LIBHEADER(FEATURE-NAME, LIB-NAME, LIB-FUNCTION, HEADER-NAME,
dnl                          ACTION-IF-FOUND, ACTION-IF-NOT-FOUND,
dnl                          EXTRA-LDFLAGS, EXTRA-CPPFLAGS)
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
AC_DEFUN(MPEG2DEC_CHECK_LIBHEADER,
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
  else
    ifelse([$6], , :, [$6])
  fi
]
)

dnl
dnl AC_CHECK_MPEG2DEC(ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)
dnl
dnl ACTION-IF-FOUND     - when feature is found then execute given action
dnl ACTION-IF-NOT-FOUND - when feature is not found then execute given action
dnl
dnl Defines HAVE_MPEG2DEC to yes if mpeg2dec is found
dnl
dnl CFLAGS and LDFLAGS for the library are stored in MPEG2DEC_CFLAGS and
dnl MPEG2DEC_LIBS, respectively
dnl
dnl Based on GST_CHECK_MPEG2DEC from gstreamer plugins 0.3.3.1
dnl Thomas Vander Stichele <thomas@apestaart.org>, Andy Wingo <wingo@pobox.com>
dnl
AC_DEFUN(AC_CHECK_MPEG2DEC, 
[dnl
AC_ARG_WITH(mpeg2dec-prefix,
    [  --with-mpeg2dec-prefix=PFX   Prefix where mpeg2dec is installed (optional)],
    mpeg2dec_config_prefix="$withval", mpeg2dec_config_prefix="")

if test x$mpeg2dec_config_prefix = x ; then
    MPEG2DEC_CHECK_LIBHEADER(MPEG2DEC, mpeg2, mpeg2_init, mpeg2dec/mpeg2.h,
    MPEG2DEC_LIBS="-lmpeg2 -lcpuaccel",, -lcpuaccel)
else
    MPEG2DEC_CHECK_LIBHEADER(MPEG2DEC, mpeg2, mpeg2_init, mpeg2dec/mpeg2.h, [
            MPEG2DEC_LIBS="-lmpeg2 -lcpuaccel -L$mpeg2dec_config_prefix/lib"
            MPEG2DEC_CFLAGS="-I$mpeg2dec_config_prefix/include"
        ], , -L$mpeg2dec_config_prefix/lib -lcpuaccel, -I$mpeg2dec_config_prefix/include)
fi
 
if test "x$HAVE_MPEG2DEC" = "xyes"; then
  ifelse([$1], , :, [$1])
else
  ifelse([$2], , :, [$2])
fi

AC_SUBST(MPEG2DEC_CFLAGS)
AC_SUBST(MPEG2DEC_LIBS)
])
