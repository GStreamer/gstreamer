-*- mode: m4 -*-
AC_PREREQ(2.57)
AC_INIT
AC_CANONICAL_TARGET

AS_VERSION(gst-python, GST_PYTHON_VERSION, 0, 1, 0, 1)
AM_INIT_AUTOMAKE($PACKAGE,$VERSION)

AC_CONFIG_SRCDIR([gstreamer/gstmodule.c])
AM_CONFIG_HEADER(config.h)

dnl Add parameters for aclocal
ACLOCAL="$ACLOCAL -I common/m4 $ACLOCAL_FLAGS"

dnl required versions of other packages
AC_SUBST(PYGTK_REQ, 2.0.0)
AC_SUBST(GLIB_REQ,  2.0.0)
AC_SUBST(GTK_REQ,   2.0.0)
AC_SUBST(GST_REQ,   0.7.5)

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
GST_MAJORMINOR=0.7
PKG_CHECK_MODULES(GST, gstreamer-$GST_MAJORMINOR >= $GST_REQ,
                  HAVE_GSTREAMER=yes,HAVE_GSTREAMER=no)

dnl Give error and exit if we don't have gstreamer
if test "x$HAVE_GSTREAMER" = "xno"; then
  AC_MSG_ERROR(you need gstreamer development packages installed !)
fi
AC_SUBST(GST_MAJORMINOR)

dnl check for pygtk
PKG_CHECK_MODULES(PYGTK, pygtk-2.0 >= $PYGTK_REQ)
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

AC_MSG_CHECKING(for GStreamer include dir)
GST_INCLUDEDIR=`$PKG_CONFIG --variable=includedir gstreamer-$GST_MAJORMINOR`
AC_MSG_RESULT($GST_INCLUDEDIR)
if test "x$GST_INCLUDEDIR" = "x"; then
  AC_MSG_ERROR(no GStreamer include dir found)
fi
AC_SUBST(GST_INCLUDEDIR)

AC_MSG_CHECKING(for GStreamer interfaces include dir)
PKG_CHECK_MODULES(GST_INTERFACES, gstreamer-interfaces-$GST_MAJORMINOR)
GST_INTERFACES_INCLUDEDIR=`$PKG_CONFIG --variable=includedir gstreamer-interfaces-$GST_MAJORMINOR`
AC_MSG_RESULT($GST_INTERFACES_INCLUDEDIR)
if test "x$GST_INTERFACES_INCLUDEDIR" = "x"; then
  AC_MSG_ERROR(no GStreamer interfaces include dir found)
fi
AC_SUBST(GST_INTERFACES_INCLUDEDIR)

AC_MSG_CHECKING(for GStreamer play include dir)
PKG_CHECK_MODULES(GST_PLAY, gstreamer-play-$GST_MAJORMINOR)
GST_PLAY_INCLUDEDIR=`$PKG_CONFIG --variable=includedir gstreamer-play-$GST_MAJORMINOR`
AC_MSG_RESULT($GST_PLAY_INCLUDEDIR)
if test "x$GST_PLAY_INCLUDEDIR" = "x"; then
  AC_MSG_ERROR(no GStreamer play include dir found)
fi
AC_SUBST(GST_PLAY_INCLUDEDIR)

AC_CHECK_PROG(HAVE_XMLTO, xmlto, true, false)
AC_CHECK_PROG(HAVE_XMLCATALOG, xmlcatalog, true, false)

AC_ARG_ENABLE(docs,
AC_HELP_STRING([--enable-docs],[enable building of documentation]),
[case "${enableval}" in
  yes)
    if test "x$HAVE_XMLTO" = "xtrue" ; then
      BUILD_DOCS=yes
    else
      AC_MSG_ERROR([you don't have xmlto, so don't use --enable-docs])
      BUILD_DOCS=no
    fi
    if test "x$HAVE_XMLCATALOG" = "xtrue" ; then
      BUILD_DOCS=yes
    else
      AC_MSG_ERROR([you don't have xmlcatalog, so don't use --enable-docs])
      BUILD_DOCS=no
    fi ;;
  no)  BUILD_DOCS=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-docs) ;;
esac],
[BUILD_DOCS=yes]) dnl Default value

AM_CONDITIONAL(BUILD_DOCS,          test "x$BUILD_DOCS" = "xyes")


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
  gstinterfaces/Makefile
  gstplay/Makefile
  pkgconfig/Makefile
  pkgconfig/gst-python.pc
  pkgconfig/gst-python-uninstalled.pc
  examples/Makefile
  examples/gstreamer/Makefile
  docs/Makefile
  docs/gst-python.ent
  testsuite/Makefile
  gst-python.spec
])
