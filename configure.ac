-*- mode: m4 -*-
AC_PREREQ(2.57)

AC_INIT
AC_CANONICAL_TARGET

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE
                                                                                
dnl when going to/from release please set the nano (fourth number) right !
AS_VERSION(gst-python, PYGST_VERSION, 0, 9, 0, 1, GST_CVS="no", GST_CVS="yes")

AM_INIT_AUTOMAKE($PACKAGE, $VERSION)

AC_DEFINE_UNQUOTED(PYGST_MAJOR_VERSION, $PYGST_VERSION_MAJOR, [PyGst major version])
AC_DEFINE_UNQUOTED(PYGST_MINOR_VERSION, $PYGST_VERSION_MINOR, [PyGst minor version])
AC_DEFINE_UNQUOTED(PYGST_MICRO_VERSION, $PYGST_VERSION_MICRO, [PyGst macro version])

AC_CONFIG_SRCDIR([gst/gstmodule.c])
AM_CONFIG_HEADER(config.h)

dnl Add parameters for aclocal
ACLOCAL="$ACLOCAL -I common/m4 $ACLOCAL_FLAGS"

dnl required versions of other packages
AC_SUBST(PYGTK_REQ, 2.6.1)
AC_SUBST(GLIB_REQ,  2.0.0)
AC_SUBST(GTK_REQ,   2.0.0)
AC_SUBST(GST_REQ,   0.9.0)


AC_DISABLE_STATIC
AC_PROG_LIBTOOL

dnl check for python
dnl AM_PATH_PYTHON(2.2)
AM_PATH_PYTHON
AC_MSG_CHECKING(for python >= 2.3)
prog="
import sys, string
minver = (2,3,0,'final',0)
if sys.version_info < minver:
  sys.exit(1)
sys.exit(0)"

if $PYTHON -c "$prog" 1>&AC_FD_CC 2>&AC_FD_CC
then
  AC_MSG_RESULT(okay)
else
  AC_MSG_ERROR(too old)
fi
AM_CHECK_PYTHON_HEADERS(,[AC_MSG_ERROR(could not find Python headers)])

dnl check for GStreamer
GST_MAJORMINOR=0.9
AC_SUBST(GST_MAJORMINOR)
PKG_CHECK_MODULES(GST, gstreamer-$GST_MAJORMINOR >= $GST_REQ)
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

dnl check for pygtk
PKG_CHECK_MODULES(PYGTK, pygtk-2.0 >= $PYGTK_REQ)
AC_SUBST(PYGTK_CFLAGS)

AC_MSG_CHECKING(for pygtk defs)
PYGTK_DEFSDIR=`$PKG_CONFIG --variable=defsdir pygtk-2.0`
AC_SUBST(PYGTK_DEFSDIR)
AC_MSG_RESULT($PYGTK_DEFSDIR)

AC_MSG_CHECKING(for pygtk h2def)
PYGTK_H2DEF=`$PKG_CONFIG --variable=codegendir pygtk-2.0`/h2def.py
AC_SUBST(PYGTK_H2DEF)
AC_MSG_RESULT($PYGTK_H2DEF)

AC_MSG_CHECKING(for pygtk codegen)
PYGTK_CODEGEN="$PYTHON `$PKG_CONFIG --variable=codegendir pygtk-2.0`/codegen.py"
dnl PYGTK_CODEGEN="$PYTHON \$(top_srcdir)/codegen/codegen.py"
AC_SUBST(PYGTK_CODEGEN)
AC_MSG_RESULT($PYGTK_CODEGEN)

dnl Interfaces
AC_MSG_CHECKING(for GStreamer interfaces include dir)
PKG_CHECK_MODULES(GST_INTERFACES, gstreamer-interfaces-$GST_MAJORMINOR,
                  HAVE_INTERFACES=yes,HAVE_INTERFACES=no)
AM_CONDITIONAL(BUILD_INTERFACES, test "x$HAVE_INTERFACES" = "xyes")
AC_SUBST(GST_INTERFACES_CFLAGS)
AC_SUBST(GST_INTERFACES_LIBS)

BUILD_DOCS=yes
AC_CHECK_PROG(XMLTO, xmlto, xmlto, no)
if test "x$XMLTO" = xno ; then
  BUILD_DOCS=no
fi
AC_SUBST(XMLTO)
AC_CHECK_PROG(XMLCATALOG, xmlcatalog, xmlcatalog, no)
if test "x$XMLCATALOG" = xno ; then
  BUILD_DOCS=no
fi
AC_SUBST(XMLCATALOG)
AC_CHECK_PROG(XMLLINT, xmllint, xmllint, no)
if test "x$XMLLINT" = xno ; then
  BUILD_DOCS=no
fi
AC_SUBST(XMLLINT)

AC_ARG_ENABLE(docs,
AC_HELP_STRING([--enable-docs],[enable building of documentation]),
[case "${enableval}" in
  yes)
    if test "x$XMLTO" = "xno" ; then
      AC_MSG_ERROR([you don't have xmlto, so don't use --enable-docs])
      BUILD_DOCS=no
    fi
    if test "x$XMLCATALOG" = "xno" ; then
      AC_MSG_ERROR([you don't have xmlcatalog, so don't use --enable-docs])
      BUILD_DOCS=no
    fi
    if test "x$XMLLINT" = "xno" ; then
      AC_MSG_ERROR([you don't have xmllint, so don't use --enable-docs])
      BUILD_DOCS=no
    fi
    ;;
  no)
    BUILD_DOCS=no
    ;;
  *)
    AC_MSG_ERROR(bad value ${enableval} for --enable-docs)
    ;;
esac])

AM_CONDITIONAL(BUILD_DOCS,          test "x$BUILD_DOCS" = "xyes")

dnl decide on error flags
dnl if we support -Wall, set it unconditionally
AS_COMPILER_FLAG(-Wall,
                 PYGST_ERROR_CFLAGS="-Wall",
                 PYGST_ERROR_CFLAGS="")
dnl if we're in nano >= 1, add -Werror if supported
if test "x$GST_CVS" = "xyes"
then
  AS_COMPILER_FLAG(-Werror, PYGST_ERROR_CFLAGS="$PYGST_ERROR_CFLAGS -Werror")
fi

PYGST_CFLAGS="$PYGST_ERROR_CFLAGS"
AC_SUBST(PYGST_CFLAGS)

dnl full installation path
AS_AC_EXPAND(PYTHONDIR, $pythondir)

dnl add debugging options ...
changequote(,)dnl
if test "x$GCC" = xyes; then
  case " $CFLAGS " in
  *[\	\ ]-Wall[\	\ ]*) ;;
  *) CFLAGS="$CFLAGS -Wall" ;;
  esac

  case " $CFLAGS " in
  *[\	\ ]-std=c9x[\	\ ]*) ;;
  *) CFLAGS="$CFLAGS -std=c9x" ;;
  esac
fi
changequote([,])dnl

AC_OUTPUT([
  Makefile
  examples/Makefile
  examples/gst/Makefile
  gst/Makefile
  pkgconfig/Makefile
  pkgconfig/gst-python.pc
  pkgconfig/gst-python-uninstalled.pc
  docs/Makefile
  docs/gst-python.ent
  testsuite/Makefile
  gst-python.spec
  pygst.py
])
