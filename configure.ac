-*- mode: m4 -*-
AC_PREREQ(2.52)

dnl the gnome-python version number
m4_define(gst_python_major_version, 0)
m4_define(gst_python_minor_version, 1)
m4_define(gst_python_micro_version, 0)
m4_define(gst_python_version, gst_python_major_version.gst_python_minor_version.gst_python_micro_version)

dnl required versions of other packages
m4_define(pygtk_required_version,             1.99.8)

m4_define(glib_required_version,              2.0.0)
m4_define(gtk_required_version,               2.0.0)
m4_define(gstreamer_required_version,         0.3.4)

AC_INIT(gst-python, gst_python_version,
        [http://gstreamer.net/])
dnl AC_CONFIG_SRCDIR([gnome/uimodule.c])
AM_CONFIG_HEADER(config.h)

AM_INIT_AUTOMAKE(AC_PACKAGE_NAME, AC_PACKAGE_VERSION)

dnl put the ACLOCAL flags in the makefile
ACLOCAL="$ACLOCAL $ACLOCAL_FLAGS"

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

dnl check for pygtk
PKG_CHECK_MODULES(PYGTK, pygtk-2.0 >= pygtk_required_version)
AC_SUBST(PYGTK_CFLAGS)
AC_PATH_PROG(PYGTK_CODEGEN, pygtk-codegen-2.0, no)
if test "x$PYGTK_CODEGEN" = xno; then
  AC_MSG_ERROR(could not find pygtk-codegen-2.0 script)
fi

AC_MSG_CHECKING(for pygtk defs)
PYGTK_DEFSDIR=`$PKG_CONFIG --variable=defsdir pygtk-2.0`
AC_SUBST(PYGTK_DEFSDIR)
AC_MSG_RESULT($PYGTK_DEFSDIR)

dnl required version substitutions for use in the spec file
PYGTK_VERSION=pygtk_required_version
GTK_VERSION=gtk_required_version
AC_SUBST(PYGTK_VERSION)
AC_SUBST(GLIB_VERSION)
AC_SUBST(GTK_VERSION)

dnl check for glib
AM_PATH_GLIB_2_0(glib_required_version,,[AC_MSG_ERROR(maybe you want the gtk-gnome-1-2 branch?)],$extra_mods)

dnl check to make sure we can find gtk
AM_PATH_GTK_2_0(gtk_required_version,,[AC_MSG_ERROR(maybe you want the gtk-gnome-1-2 branch?)],$extra_mods)

dnl check to make sure we can find gstreamer
PKG_CHECK_MODULES(GST, gstreamer >= gstreamer_required_version, HAVE_GST="yes", HAVE_GST="no")

if test "x$HAVE_GST" = "xno"; then
  AC_MSG_ERROR(need GStreamer $gstreamer_required_version)
fi

AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

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
dnl
AC_CONFIG_FILES(
  Makefile
  gstreamer/Makefile
  examples/Makefile
  examples/gstreamer/Makefile
  gst-python.spec)
AC_OUTPUT
