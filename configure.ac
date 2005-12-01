-*- mode: m4 -*-
AC_PREREQ(2.57)

AC_INIT
AC_CANONICAL_TARGET

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE
                                                                                
dnl when going to/from release please set the nano (fourth number) right !
AS_VERSION(gst-python, PYGST_VERSION, 0, 9, 7, 1,
    GST_CVS="no", GST_CVS="yes")

AM_INIT_AUTOMAKE($PACKAGE, $VERSION)

AC_DEFINE_UNQUOTED(PYGST_MAJOR_VERSION, $PYGST_VERSION_MAJOR, [PyGst major version])
AC_DEFINE_UNQUOTED(PYGST_MINOR_VERSION, $PYGST_VERSION_MINOR, [PyGst minor version])
AC_DEFINE_UNQUOTED(PYGST_MICRO_VERSION, $PYGST_VERSION_MICRO, [PyGst macro version])

AC_CONFIG_SRCDIR([gst/gstmodule.c])
AM_CONFIG_HEADER(config.h)

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I common/m4")

dnl required versions of other packages
AC_SUBST(PYGTK_REQ, 2.6.3)
AC_SUBST(GLIB_REQ,  2.6.0)
AC_SUBST(GTK_REQ,   2.6.0)
AC_SUBST(GST_REQ,   0.9.7)


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
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)
PKG_CHECK_MODULES(GST, gstreamer-$GST_MAJORMINOR >= $GST_REQ)
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

dnl check for gstreamer-base; uninstalled is selected preferentially
PKG_CHECK_MODULES(GST_BASE, gstreamer-base-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_BASE="yes", HAVE_GST_BASE="no")

if test "x$HAVE_GST_BASE" = "xno"; then
  AC_MSG_ERROR(no GStreamer Base Libs found)
fi

AC_SUBST(GST_BASE_LIBS)
AC_SUBST(GST_BASE_CFLAGS)

dnl check for gstreamer-controller
PKG_CHECK_MODULES(GST_CONTROLLER, gstreamer-controller-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_CONTROLLER="yes", HAVE_GST_CONTROLLER="no")

if test "x$HAVE_GST_CONTROLLER" = "xno"; then
  AC_MSG_ERROR(no GStreamer Controller Libs found)
fi

AC_SUBST(GST_CONTROLLER_LIBS)
AC_SUBST(GST_CONTROLLER_CFLAGS)

dnl check for gstreamer-net
PKG_CHECK_MODULES(GST_NET, gstreamer-net-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_NET="yes", HAVE_GST_NET="no")

if test "x$HAVE_GST_NET" = "xno"; then
  AC_MSG_ERROR(no GStreamer Networking Libs found)
fi

AC_SUBST(GST_NET_LIBS)
AC_SUBST(GST_NET_CFLAGS)

dnl check for gstreamer-net
PKG_CHECK_MODULES(GST_PLUGINS_BASE, gstreamer-plugins-base-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_PLUGINS_BASE="yes", HAVE_GST_PLUGINS_BASE="no")

if test "x$HAVE_GST_PLUGINS_BASE" = "xno"; then
  AC_MSG_ERROR(no gst-plugins-base found)
fi

AC_SUBST(GST_PLUGINS_BASE_LIBS)
AC_SUBST(GST_PLUGINS_BASE_CFLAGS)


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

GST_VALGRIND_CHECK()

AC_OUTPUT([
  Makefile
  codegen/Makefile
  gst/Makefile
  gst/extend/Makefile
  examples/Makefile
  pkgconfig/Makefile
  pkgconfig/gst-python.pc
  pkgconfig/gst-python-uninstalled.pc
  testsuite/Makefile
  gst-python.spec
])
