-*- mode: m4 -*-
AC_PREREQ(2.54)
AC_INIT
AC_CANONICAL_TARGET

AS_VERSION(gst-python, GST_PYTHON_VERSION, 0, 1, 0, 0)
AM_INIT_AUTOMAKE($PACKAGE,$VERSION)

AC_CONFIG_SRCDIR([gstreamer/gstreamermodule.c])
AM_CONFIG_HEADER(config.h)

dnl Add parameters for aclocal
ACLOCAL="$ACLOCAL -I common/m4 $ACLOCAL_FLAGS"

dnl required versions of other packages
AC_SUBST(PYGTK_REQ, 1.99.14)
AC_SUBST(GLIB_REQ,  2.0.0)
AC_SUBST(GTK_REQ,   2.0.0)
AC_SUBST(GST_REQ,   0.6.0)

AC_DISABLE_STATIC
AC_PROG_LIBTOOL

dnl check for python
dnl AM_PATH_PYTHON(2.2)
AM_PATH_PYTHON
AC_MSG_CHECKING(for python >= 2.2)
prog="
import sys, string
minver = (2,2,0,'final',0)
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
dnl start with 0.7
GST_MAJORMINOR=0.7
PKG_CHECK_MODULES(GST, gstreamer-$GST_MAJORMINOR >= $GST_REQ,
                  HAVE_GSTREAMER=yes,HAVE_GSTREAMER=no)

dnl try 0.6
if test "x$HAVE_GSTREAMER" = "xno"; then
        GST_MAJORMINOR=0.6
        PKG_CHECK_MODULES(GST, gstreamer-$GST_MAJORMINOR >= $GST_REQ,
                          HAVE_GSTREAMER=yes,HAVE_GSTREAMER=no)
fi

dnl Give error and exit if we don't have gstreamer
if test "x$HAVE_GSTREAMER" = "xno"; then
        AC_MSG_ERROR(you need gstreamer development packages installed !)
fi

AC_SUBST(GST_LIBS)
AC_SUBST(GST_CFLAGS)

dnl check for pygtk
PKG_CHECK_MODULES(PYGTK, pygtk-2.0 >= $PYGTK_REQ)
AC_SUBST(PYGTK_CFLAGS)
AC_PATH_PROG(PYGTK_CODEGEN, pygtk-codegen-2.0, no)
if test "x$PYGTK_CODEGEN" = xno; then
  AC_MSG_ERROR(could not find pygtk-codegen-2.0 script)
fi

AC_MSG_CHECKING(for pygtk defs)
PYGTK_DEFSDIR=`$PKG_CONFIG --variable=defsdir pygtk-2.0`
AC_SUBST(PYGTK_DEFSDIR)
AC_MSG_RESULT($PYGTK_DEFSDIR)

AC_MSG_CHECKING(for pygtk h2def)
PYGTK_H2DEF=`$PKG_CONFIG --variable=datadir pygtk-2.0`/pygtk/2.0/codegen/h2def.py
AC_SUBST(PYGTK_H2DEF)
AC_MSG_RESULT($PYGTK_H2DEF)

#dnl check for glib
#AM_PATH_GLIB_2_0(glib_required_version,,[AC_MSG_ERROR(maybe you want the gtk-gnome-1-2 branch?)],$extra_mods)
#
#dnl check to make sure we can find gtk
#AM_PATH_GTK_2_0(gtk_required_version,,[AC_MSG_ERROR(maybe you want the gtk-gnome-1-2 branch?)],$extra_mods)

AC_MSG_CHECKING(for gstreamer include dir)
GST_INCLUDEDIR=`$PKG_CONFIG --variable=includedir gstreamer-$GST_MAJORMINOR`

AC_SUBST(GST_INCLUDEDIR)
AC_MSG_RESULT($GST_INCLUDEDIR)
if test "x$GST_INCLUDEDIR" = "x"; then
  AC_MSG_ERROR(no gstreamer include dir found)
fi

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
  gstreamer/Makefile
  examples/Makefile
  examples/gstreamer/Makefile
  gst-python.spec
])
