dnl
dnl A52_CHECK-LIBHEADER(FEATURE-NAME, LIB-NAME, LIB-FUNCTION, HEADER-NAME,
dnl                     ACTION-IF-FOUND, ACTION-IF-NOT-FOUND,
dnl                     EXTRA-LDFLAGS, EXTRA-CPPFLAGS)
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
AC_DEFUN([A52_CHECK_LIBHEADER],
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
dnl AC_CHECK_A52DEC(ACTION-IF-FOUND, ACTION-IF-NOT-FOUND)
dnl
dnl ACTION-IF-FOUND     - when feature is found then execute given action
dnl ACTION-IF-NOT-FOUND - when feature is not found then execute given action
dnl
dnl Defines HAVE_A52DEC to yes if liba52 is found
dnl
dnl CFLAGS and LDFLAGS for the library are stored in A52DEC_CFLAGS and
dnl A52DEC_LIBS, respectively
dnl
dnl Based on GST_CHECK_A52DEC from gstreamer plugins 0.3.3.1
dnl Thomas Vander Stichele <thomas@apestaart.org>, Andy Wingo <wingo@pobox.com>
dnl
AC_DEFUN([AC_CHECK_A52DEC], 
[dnl
AC_ARG_WITH(a52dec-prefix,
    AC_HELP_STRING([--with-a52dec-prefix=PFX],
                   [prefix where a52dec is installed (optional)]),
    a52dec_config_prefix="$withval", a52dec_config_prefix="")

if test x$a52dec_config_prefix = x ; then
    A52_CHECK_LIBHEADER(A52DEC, a52, a52_init, a52dec/a52.h,
        A52DEC_LIBS="-la52 -lm", , -lm)
else
    A52_CHECK_LIBHEADER(A52DEC, a52, a52_init, a52dec/a52.h, [
            A52DEC_LIBS="-la52 -L$a52dec_config_prefix/lib -lm"
            A52DEC_CFLAGS="-I$a52dec_config_prefix/include"
        ], , -L$a52dec_config_prefix/lib, -I$a52dec_config_prefix/include)
fi

if test $HAVE_A52DEC = "yes"; then
    ac_save_CFLAGS="$CFLAGS"
    ac_save_LIBS="$LIBS"
    CFLAGS="$CFLAGS $A52DEC_CFLAGS"
    LIBS="$A52DEC_LIBS $LIBS"
    AC_TRY_RUN([
#include <inttypes.h>
#include <a52dec/a52.h>

int 
main ()
{
  a52_state_t *state;
#if defined(A52_ACCEL_DETECT)
  state = a52_init ();
#else
  state = a52_init (0);
#endif
  a52_free (state);
  return 0;
}
        ],, HAVE_A52DEC=no, [echo $ac_n "cross compiling; assumed OK... $ac_c"])

    if test HAVE_A52DEC = "no"; then
        echo "*** Your a52dec is borked somehow. Please update to 0.7.4 or newer."
    else
        AC_TRY_RUN([
#include <inttypes.h>
#include <a52dec/a52.h>

int 
main ()
{
  int i = sizeof (a52_state_t);
  if ( i )
    return 0;
}
            ], HAVE_A52DEC=no,, [echo $ac_n "cross compiling; assumed OK... $ac_c"])

        if test HAVE_A52DEC = "no"; then
            echo "*** Your a52dec is too old. Please update to 0.7.4 or newer."
        fi
    fi
    CFLAGS="$ac_save_CFLAGS"
    LIBS="$ac_save_LIBS"
fi

if test HAVE_A52DEC = "no"; then
    A52DEC_CFLAGS=""
    A52DEC_LIBS=""
fi

if test "x$HAVE_A52DEC" = "xyes"; then
  ifelse([$1], , :, [$1])
else
  ifelse([$2], , :, [$2])
fi

AC_SUBST(A52DEC_CFLAGS)
AC_SUBST(A52DEC_LIBS)
])
