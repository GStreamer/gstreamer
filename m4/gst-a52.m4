AC_DEFUN(GST_CHECK_A52DEC, 
[dnl
AC_ARG_WITH(a52dec-prefix,
    AC_HELP_STRING([--with-a52dec-prefix=PFX],[Prefix where a52dec is installed (optional)]),
    a52dec_config_prefix="$withval", a52dec_config_prefix="")

if test x$a52dec_config_prefix = x ; then
    CHECK_LIBHEADER(A52DEC, a52, a52_init, a52dec/a52.h,
        A52DEC_LIBS="-la52 -lm", , -lm)
else
    CHECK_LIBHEADER(A52DEC, a52, a52_init, a52dec/a52.h, [
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
  return 0;
}
        ],, HAVE_A52DEC=no, [echo $ac_n "cross compiling; assumed OK... $ac_c"])

    if test HAVE_A52DEC = "no"; then
        echo "*** Your a52dec is borked somehow. Please update to 0.7.3."
    else
        AC_TRY_RUN([
#include <inttypes.h>
#include <a52dec/a52.h>

int 
main ()
{
  int i = sizeof (a52_state_t);
  return 0;
}
            ], HAVE_A52DEC=no,, [echo $ac_n "cross compiling; assumed OK... $ac_c"])

        if test HAVE_A52DEC = "no"; then
            echo "*** Your a52dec is too old. Please update to 0.7.3."
        fi
    fi
    CFLAGS="$ac_save_CFLAGS"
    LIBS="$ac_save_LIBS"
fi

if test HAVE_A52DEC = "no"; then
    A52DEC_CFLAGS=""
    A52DEC_LIBS=""
fi

AC_SUBST(A52DEC_CFLAGS)
AC_SUBST(A52DEC_LIBS)
AC_SUBST(HAVE_A52DEC)
])
