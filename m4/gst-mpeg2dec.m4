AC_DEFUN(GST_CHECK_MPEG2DEC, 
[dnl
AC_ARG_WITH(mpeg2dec-prefix,
    [  --with-mpeg2dec-prefix=PFX   Prefix where mpeg2dec is installed (optional)],
    mpeg2dec_config_prefix="$withval", mpeg2dec_config_prefix="")

if test x$mpeg2dec_config_prefix = x ; then
    CHECK_LIBHEADER(MPEG2DEC, mpeg2, mpeg2_init, mpeg2dec/mpeg2.h,
        MPEG2DEC_LIBS="-lmpeg2 -lmpeg2dec")
else
    CHECK_LIBHEADER(MPEG2DEC, mpeg2, mpeg2_init, mpeg2dec/mpeg2.h, [
            MPEG2DEC_LIBS="-lmpeg2 -lmpeg2dec -L$mpeg2dec_config_prefix/lib"
            MPEG2DEC_CFLAGS="-I$mpeg2dec_config_prefix/include"
        ], , -L$mpeg2dec_config_prefix/lib, -I$mpeg2dec_config_prefix/include)
fi
 
AC_SUBST(MPEG2DEC_CFLAGS)
AC_SUBST(MPEG2DEC_LIBS)
])
