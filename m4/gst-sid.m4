dnl FIXME: the longest macro for one of the small plugins ?
dnl there must be a way to make this simpler !
dnl sidplay stuff (taken from xmms)
AC_DEFUN(AC_FIND_FILE,
[ 
  $3=NO
  for i in $2; do
    for j in $1; do
      if test -r "$i/$j"; then
        $3=$i
        break 2
      fi
    done
  done
])

AC_DEFUN(AC_PATH_LIBSIDPLAY,
[
AC_MSG_CHECKING([for SIDPLAY includes and library])
ac_sidplay_cflags=NO
ac_sidplay_library=NO
sidplay_cflags=""
sidplay_library=""
AC_ARG_WITH(sidplay-includes,
  [  --with-sidplay-includes=DIR
                          where the sidplay includes are located],
  [ac_sidplay_cflags="$withval"
  ])

AC_ARG_WITH(sidplay-library,
  [  --with-sidplay-library=DIR
                          where the sidplay library is installed],
  [ac_sidplay_library="$withval"
  ])
  
if test "$ac_sidplay_cflags" = NO || test "$ac_sidplay_library" = NO; then

#search common locations
    
AC_CACHE_VAL(ac_cv_have_sidplay,
[
sidplay_incdirs="$ac_sidplay_cflags /usr/include /usr/local/include /usr/lib/sidplay/include /usr/local/lib/sidplay/include"
AC_FIND_FILE(sidplay/sidtune.h,$sidplay_incdirs,sidplay_foundincdir)
sidplay_libdirs="$ac_sidplay_library /usr/lib /usr/local/lib /usr/lib/sidplay /usr/local/lib/sidplay"
AC_FIND_FILE(libsidplay.so libsidplay.so.1 libsidplay.so.1.36 libsidplay.so.1.37,$sidplay_libdirs,sidplay_foundlibdir)
ac_sidplay_library=$sidplay_foundlibdir

if test "$ac_sidplay_cflags" = NO || test "$ac_sidplay_library" = NO; then
  ac_cv_have_sidplay="have_sidplay=no"
  ac_sidplay_notfound=""
  if test "$ac_sidplay_cflags" = NO; then
    if test "$ac_sidplay_library" = NO; then
      ac_sidplay_notfound="(headers and library)";
    else
      ac_sidplay_notfound="(headers)";
    fi
  else
    ac_sidplay_notfound="(library)";
  fi
  eval "$ac_cv_have_sidplay"
  AC_MSG_RESULT([$have_sidplay])
else
  have_sidplay=yes
fi
      
])  dnl AC_CACHE_VAL(ac_cv_have_sidplay,
else  
  have_sidplay=yes
fi  dnl if (have_to_search)
  
eval "$ac_cv_have_sidplay"
if test "$have_sidplay" != yes; then
  AC_MSG_RESULT([$have_sidplay]);
else  
  ac_cv_have_sidplay="have_sidplay=yes \
    ac_sidplay_cflags=$ac_sidplay_cflags 
ac_sidplay_library=$ac_sidplay_library"
  AC_MSG_RESULT([library $ac_sidplay_library, headers $ac_sidplay_cflags])

  sidplay_library=$ac_sidplay_library
  sidplay_cflags=$ac_sidplay_cflags
  SIDPLAY_LIBS="-L$sidplay_library -lsidplay"
  all_libraries="$SIDPLAY_LIBS $all_libraries"
  SIDPLAY_CFLAGS="-I$sidplay_cflags"
  all_includes="$SIDPLAY_CFLAGS $all_includes"
fi
  
dnl Test compilation.
  
AC_MSG_CHECKING([whether -lsidplay works])
ac_cxxflags_safe=$CXXFLAGS
ac_ldflags_safe=$LDFLAGS
ac_libs_safe=$LIBS
  
CXXFLAGS="$CXXFLAGS -I$sidplay_cflags"
LDFLAGS="$LDFLAGS -L$sidplay_library"
LIBS="-lsidplay"

AC_CACHE_VAL(ac_cv_sidplay_works,
[

  AC_LANG_CPLUSPLUS
  AC_TRY_RUN([
    #include <sidplay/player.h>
  
    int main()
    {
      sidTune tune = sidTune(0);
    }
    ],
    ac_cv_sidplay_works="yes",
    ac_cv_sidplay_works="no",
    ac_cv_sidplay_works="no")
  AC_LANG_C
])
    
CXXFLAGS="$ac_cxxflags_safe"
LDFLAGS="$ac_ldflags_safe"
LIBS="$ac_libs_safe"
      
AC_MSG_RESULT([$ac_cv_sidplay_works])

  have_sidplay=no
fi
  
AC_SUBST(SIDPLAY_CFLAGS)  
AC_SUBST(SIDPLAY_LIBS)
      
AC_SUBST(sidplay_library)
AC_SUBST(sidplay_cflags)
  
])

