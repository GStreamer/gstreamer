## this one is commonly used with AM_PATH_PYTHONDIR ...
dnl AM_CHECK_PYMOD(MODNAME [,SYMBOL [,ACTION-IF-FOUND [,ACTION-IF-NOT-FOUND]]])
dnl Check if a module containing a given symbol is visible to python.
AC_DEFUN([AM_CHECK_PYMOD],
[AC_REQUIRE([AM_PATH_PYTHON])
py_mod_var=`echo $1['_']$2 | sed 'y%./+-%__p_%'`
AC_MSG_CHECKING(for ifelse([$2],[],,[$2 in ])python module $1)
AC_CACHE_VAL(py_cv_mod_$py_mod_var, [
ifelse([$2],[], [prog="
import sys
try:
        import $1
except ImportError:
        sys.exit(1)
except:
        sys.exit(0)
sys.exit(0)"], [prog="
import $1
$1.$2"])
if $PYTHON -c "$prog" 1>&AC_FD_CC 2>&AC_FD_CC
  then
    eval "py_cv_mod_$py_mod_var=yes"
  else
    eval "py_cv_mod_$py_mod_var=no"
  fi
])
py_val=`eval "echo \`echo '$py_cv_mod_'$py_mod_var\`"`
if test "x$py_val" != xno; then
  AC_MSG_RESULT(yes)
  ifelse([$3], [],, [$3
])dnl
else
  AC_MSG_RESULT(no)
  ifelse([$4], [],, [$4
])dnl
fi
])

dnl a macro to check for ability to create python extensions
dnl  AM_CHECK_PYTHON_HEADERS([ACTION-IF-POSSIBLE], [ACTION-IF-NOT-POSSIBLE])
dnl function also defines PYTHON_INCLUDES
AC_DEFUN([AM_CHECK_PYTHON_HEADERS],
[AC_REQUIRE([AM_PATH_PYTHON])
AC_MSG_CHECKING(for headers required to compile python extensions)
dnl deduce PYTHON_INCLUDES
py_prefix=`$PYTHON -c "import sys; print(sys.prefix)"`
py_exec_prefix=`$PYTHON -c "import sys; print(sys.exec_prefix)"`
if $PYTHON-config --help 1>/dev/null 2>/dev/null; then
  PYTHON_INCLUDES=`$PYTHON-config --includes 2>/dev/null`
  if $PYTHON-config --abiflags 1>/dev/null 2>/dev/null; then
    PYTHON_ABI_FLAGS=`$PYTHON-config --abiflags 2>/dev/null`
  else
    PYTHON_ABI_FLAGS=
  fi
else
  PYTHON_INCLUDES="-I${py_prefix}/include/python${PYTHON_VERSION}"
  if test "$py_prefix" != "$py_exec_prefix"; then
    PYTHON_INCLUDES="$PYTHON_INCLUDES -I${py_exec_prefix}/include/python${PYTHON_VERSION}"
  fi
fi
AC_SUBST(PYTHON_INCLUDES)
dnl check if the headers exist:
save_CPPFLAGS="$CPPFLAGS"
CPPFLAGS="$CPPFLAGS $PYTHON_INCLUDES"
AC_TRY_CPP([#include <Python.h>],dnl
[AC_MSG_RESULT(found)
$1],dnl
[AC_MSG_RESULT(not found)
$2])
CPPFLAGS="$save_CPPFLAGS"
])

dnl a macro to check for ability to embed python
dnl  AM_CHECK_PYTHON_LIBS([ACTION-IF-POSSIBLE], [ACTION-IF-NOT-POSSIBLE])
dnl function also defines PYTHON_LIBS
AC_DEFUN([AM_CHECK_PYTHON_LIBS],
[AC_REQUIRE([AM_CHECK_PYTHON_HEADERS])
AC_MSG_CHECKING(for libraries required to embed python)

dnl deduce PYTHON_LIBS
if $PYTHON-config --help 1>/dev/null 2>/dev/null; then
  dnl Need to pass --embed in python 3.8 or newer:
  dnl https://docs.python.org/dev/whatsnew/3.8.html#debug-build-uses-the-same-abi-as-release-build
  if $PYTHON-config --embed 1>/dev/null 2>/dev/null; then
    python_config_embed_flag="--embed"
  else
    python_config_embed_flag=""
  fi
  PYTHON_LIBS=`$PYTHON-config --ldflags $python_config_embed_flag 2>/dev/null`
  PYTHON_LIB=`$PYTHON -c "import distutils.sysconfig as s; print(s.get_python_lib(standard_lib=1))"`
  if echo "$host_os" | grep darwin >/dev/null 2>&1; then
    dnl OSX is a pain. Python as shipped by apple installs libpython in /usr/lib
    dnl so we hardcode that. Other systems can use --with-libpython-dir to
    dnl overrid this.
    PYTHON_LIB_LOC=/usr/lib
  else
    PYTHON_LIB_LOC=$PYTHON_LIB/config

    # default to prefix/lib for distros that don't have a link in
    # .../pythonX.Y/config/
    if test ! -e $PYTHON_LIB_LOC/libpython${PYTHON_VERSION}${PYTHON_ABI_FLAGS}.so; then
      if test -e ${py_prefix}/lib64/libpython${PYTHON_VERSION}${PYTHON_ABI_FLAGS}.so; then
        PYTHON_LIB_LOC=${py_prefix}/lib64
      else
        PYTHON_LIB_LOC=${py_prefix}/lib
      fi
    fi
  fi
else
  PYTHON_LIBS="-L${py_prefix}/lib -lpython${PYTHON_VERSION}"
  PYTHON_LIB_LOC="${py_prefix}/lib"
fi

AC_ARG_WITH([libpython-dir],
  AS_HELP_STRING([--with-libpython-dir], [the directory containing libpython${PYTHON_VERSION}]),
  [
    PYTHON_LIB_LOC=`echo "$withval" | sed -e 's/\/$//g'`
  ]
)

if echo "$host_os" | grep darwin >/dev/null 2>&1; then
  dnl workaround libtool brokenness under OSX
  PYTHON_LIB_SUFFIX=\\\"dylib\\\"
else
  PYTHON_LIB_SUFFIX=G_MODULE_SUFFIX
fi

AC_SUBST(PYTHON_LIBS)
AC_SUBST(PYTHON_LIB_LOC)
AC_SUBST(PYTHON_ABI_FLAGS)
AC_SUBST(PYTHON_LIB_SUFFIX)
dnl check if the headers exist:
save_LIBS="$LIBS"
LIBS="$LIBS $PYTHON_LIBS"
AC_TRY_LINK_FUNC(Py_Initialize, dnl
         [LIBS="$save_LIBS"; AC_MSG_RESULT(yes); $1], dnl
         [LIBS="$save_LIBS"; AC_MSG_RESULT(no); $2])

])
