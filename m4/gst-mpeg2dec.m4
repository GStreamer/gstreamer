AC_DEFUN(GST_CHECK_MPEG2DEC, 
[dnl
dnl
dnl check for mpeg2dec in standard location
dnl if not found then check for mpeg2dec in /usr/X11R6/lib
dnl
CHECK_LIBHEADER(MPEG2DEC, mpeg2, mpeg2_init, mpeg2dec/mpeg2.h, MPEG2DEC_LIBS="-lmpeg2 -lmpeg2dec")
 
dnl unset cache variable - we want to check once again for the same library
dnl but in different location
unset ac_cv_lib_mpeg2_mpeg2_init

dnl check again in /usr/X11R6/lib
if test x$HAVE_MPEG2DEC = xno; then
    AC_MSG_NOTICE([NOTICE: mpeg2dec not found, let's try again in /usr/X11R6])
    CHECK_LIBHEADER(MPEG2DEC, mpeg2, mpeg2_init, mpeg2dec/mpeg2.h, [
            MPEG2DEC_LIBS="-lmpeg2 -lmpeg2dec -L/usr/X11R6/lib"
            MPEG2DEC_CFLAGS="-I/usr/X11R6/include"
        ], , -L/usr/X11R6/lib, -I/usr/X11R6/include)
fi

AC_SUBST(MPEG2DEC_CFLAGS)
AC_SUBST(MPEG2DEC_LIBS)
])
