AC_PREREQ(2.60)

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT(GStreamer Python Bindings, 0.10.21.1,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gst-python)

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability 1.10])

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_CVS="no", GST_CVS="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/gstmodule.c])

dnl define the output header for config
AM_CONFIG_HEADER([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST(AM_DEFAULT_VERBOSITY)])

AC_DEFINE_UNQUOTED(PYGST_MAJOR_VERSION, $PACKAGE_VERSION_MAJOR, [PyGst major version])
AC_DEFINE_UNQUOTED(PYGST_MINOR_VERSION, $PACKAGE_VERSION_MINOR, [PyGst minor version])
AC_DEFINE_UNQUOTED(PYGST_MICRO_VERSION, $PACKAGE_VERSION_MICRO, [PyGst micro version])
AC_DEFINE_UNQUOTED(PYGST_NANO_VERSION, $PACKAGE_VERSION_NANO, [PyGst nano version])
AC_SUBST(PACKAGE_VERSION_MAJOR)
AC_SUBST(PACKAGE_VERSION_MINOR)
AC_SUBST(PACKAGE_VERSION_MICRO)
AC_SUBST(PACKAGE_VERSION_NANO)

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl required versions of other packages
AC_SUBST(PYGTK_REQ, 2.6.3)
AC_SUBST(PYGOBJECT_REQ, 2.11.2)
AC_SUBST(GLIB_REQ,  2.8.0)
AC_SUBST(GTK_REQ,   2.6.0)
AC_SUBST(GST_REQ,   0.10.20)
AC_SUBST(GSTPB_REQ, 0.10.20)

AC_DISABLE_STATIC

AC_LIBTOOL_WIN32_DLL
AC_PROG_LIBTOOL

dnl find a compiler
AC_PROG_CC
AC_PROG_CC_STDC

dnl check if the compiler supports '-c' and '-o' options
AM_PROG_CC_C_O

AG_GST_ARG_GCOV

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
AC_DEFINE_UNQUOTED(GST_MAJORMINOR, "$GST_MAJORMINOR", [Gst MajorMinor version])
GST_CFLAGS="$GST_CFLAGS $GLIB_EXTRA_CFLAGS"
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

dnl get the installed GStreamer core version
GST_MINOR_VERSION=`$PKG_CONFIG --modversion gstreamer-$GST_MAJORMINOR | cut -f 3 -d.`
GST_PB_MINOR_VERSION=`$PKG_CONFIG --modversion gstreamer-plugins-base-$GST_MAJORMINOR | cut -f 3 -d.`
GST_CVS_VERSION=`$PKG_CONFIG --modversion gstreamer-$GST_MAJORMINOR | cut -f 4 -d.`

echo "Building against GStreamer core 0.10.$GST_MINOR_VERSION , ignoring API additions if needed"

dnl Magic for allowing new API additions without forcing dependency on new core
dnl release. The lines do the magic so that new API additions are ignored at
dnl compile time.
dnl * Also see gst/gstversion.override.in and gst-0.10.*.override
dnl The following lines should be updated whenever:
dnl _ GST_REQ is up-ed (remove obsolete lines + gst-0.10.MINOR.ignore)
dnl _ new core/base is released (add lines + gst-0.10.MINOR.ignore)

if test "x$GST_CVS_VERSION" = "x"
then
  if test $GST_MINOR_VERSION -lt "21"
  then
    IGNORE_GST_0_10_21="gst-0.10.21.ignore"
  else
    IGNORE_GST_0_10_21=""
  fi

  if test $GST_MINOR_VERSION -lt "22"
  then
    IGNORE_GST_0_10_22="gst-0.10.22.ignore"
  else
    IGNORE_GST_0_10_22=""
  fi

  if test $GST_MINOR_VERSION -lt "23"
  then
    IGNORE_GST_0_10_23="gst-0.10.23.ignore"
  else
    IGNORE_GST_0_10_23=""
  fi

  if test $GST_MINOR_VERSION -lt "24"
  then
    IGNORE_GST_0_10_24="gst-0.10.24.ignore"
  else
    IGNORE_GST_0_10_24=""
  fi

  if test $GST_MINOR_VERSION -lt "25"
  then
    IGNORE_GST_0_10_25="gst-0.10.25.ignore"
  else
    IGNORE_GST_0_10_25=""
  fi

  if test $GST_MINOR_VERSION -lt "26"
  then
    IGNORE_GST_0_10_26="gst-0.10.26.ignore"
  else
    IGNORE_GST_0_10_26=""
  fi

  if test $GST_MINOR_VERSION -lt "29"
  then
    IGNORE_GST_0_10_29="gst-0.10.29.ignore"
  else
    IGNORE_GST_0_10_29=""
  fi

  if test $GST_MINOR_VERSION -lt "30"
  then
    IGNORE_GST_0_10_30="gst-0.10.30.ignore"
  else
    IGNORE_GST_0_10_30=""
  fi

  if test $GST_MINOR_VERSION -lt "31"
  then
    IGNORE_GST_0_10_31="gst-0.10.31.ignore"
  else
    IGNORE_GST_0_10_31=""
  fi

  if test $GST_MINOR_VERSION -lt "32"
  then
    IGNORE_GST_0_10_32="gst-0.10.32.ignore"
  else
    IGNORE_GST_0_10_32=""
  fi

  dnl plugins base
  if test $GST_PB_MINOR_VERSION -ge "22"
  then
    AC_DEFINE_UNQUOTED(HAVE_GST_AUDIO, 1, [We can use the gst-audio library])
    AC_DEFINE_UNQUOTED(HAVE_GST_VIDEO, 1, [We can use the gst-video library])
    AC_DEFINE_UNQUOTED(HAVE_GST_TAG, 1, [We can use the gst-tag library])
  fi

  if test $GST_PB_MINOR_VERSION -lt "23"
  then
    IGNORE_GST_PB_0_10_23="gst-pb-0.10.23.ignore"
  else
    IGNORE_GST_PB_0_10_23=""
  fi

  if test $GST_PB_MINOR_VERSION -lt "25"
  then
    IGNORE_GST_PB_0_10_25="gst-pb-0.10.25.ignore"
  else
    IGNORE_GST_PB_0_10_25=""
    AC_DEFINE_UNQUOTED(HAVE_STREAM_VOLUME_INTERFACE, 1, [We can use the streamvolume interface])
  fi 

  if test $GST_PB_MINOR_VERSION -lt "26"
  then
    IGNORE_GST_PB_0_10_26="gst-pb-0.10.26.ignore"
  else
    IGNORE_GST_PB_0_10_26=""
  fi

  if test $GST_PB_MINOR_VERSION -lt "29"
  then
    IGNORE_GST_PB_0_10_29="gst-pb-0.10.29.ignore"
  else
    IGNORE_GST_PB_0_10_29=""
  fi

  if test $GST_PB_MINOR_VERSION -lt "30"
  then
    IGNORE_GST_PB_0_10_30="gst-pb-0.10.30.ignore"
  else
    IGNORE_GST_PB_0_10_30=""
  fi

  if test $GST_PB_MINOR_VERSION -lt "31"
  then
    IGNORE_GST_PB_0_10_31="gst-pb-0.10.31.ignore"
  else
    IGNORE_GST_PB_0_10_31=""
  fi

  if test $GST_PB_MINOR_VERSION -lt "32"
  then
    IGNORE_GST_PB_0_10_32="gst-pb-0.10.32.ignore"
  else
    IGNORE_GST_PB_0_10_32=""
  fi

else
  IGNORE_GST_0_10_18=""
  IGNORE_GST_0_10_21=""
  IGNORE_GST_0_10_22=""
  IGNORE_GST_0_10_23=""
  IGNORE_GST_0_10_24=""
  IGNORE_GST_0_10_25=""
  IGNORE_GST_0_10_26=""
  IGNORE_GST_0_10_29=""
  IGNORE_GST_0_10_30=""
  IGNORE_GST_0_10_31=""
  IGNORE_GST_0_10_32=""
  IGNORE_GST_PB_0_10_23=""
  IGNORE_GST_PB_0_10_25=""
  IGNORE_GST_PB_0_10_26=""
  IGNORE_GST_PB_0_10_29=""
  IGNORE_GST_PB_0_10_30=""
  IGNORE_GST_PB_0_10_31=""
  IGNORE_GST_PB_0_10_32=""
  AC_DEFINE_UNQUOTED(HAVE_GST_AUDIO, 1, [We can use the gst-audio library])
  AC_DEFINE_UNQUOTED(HAVE_GST_VIDEO, 1, [We can use the gst-video library])
  AC_DEFINE_UNQUOTED(HAVE_GST_TAG, 1, [We can use the gst-tag library])
fi
AC_DEFINE_UNQUOTED(HAVE_STREAM_VOLUME_INTERFACE, 1, [We can use the streamvolume interface])
AC_SUBST(IGNORE_GST_0_10_21)
AC_SUBST(IGNORE_GST_0_10_22)
AC_SUBST(IGNORE_GST_0_10_23)
AC_SUBST(IGNORE_GST_0_10_24)
AC_SUBST(IGNORE_GST_0_10_25)
AC_SUBST(IGNORE_GST_0_10_26)
AC_SUBST(IGNORE_GST_0_10_29)
AC_SUBST(IGNORE_GST_0_10_30)
AC_SUBST(IGNORE_GST_0_10_31)
AC_SUBST(IGNORE_GST_0_10_32)
AC_SUBST(IGNORE_GST_PB_0_10_23)
AC_SUBST(IGNORE_GST_PB_0_10_25)
AC_SUBST(IGNORE_GST_PB_0_10_26)
AC_SUBST(IGNORE_GST_PB_0_10_29)
AC_SUBST(IGNORE_GST_PB_0_10_30)
AC_SUBST(IGNORE_GST_PB_0_10_31)
AC_SUBST(IGNORE_GST_PB_0_10_32)
AM_CONDITIONAL(HAVE_GST_AUDIO, $HAVE_GST_AUDIO)
AM_CONDITIONAL(HAVE_GST_VIDEO, $HAVE_GST_VIDEO)
AM_CONDITIONAL(HAVE_GST_TAG, $HAVE_GST_TAG)

dnl check for gstreamer-base; uninstalled is selected preferentially
PKG_CHECK_MODULES(GST_BASE, gstreamer-base-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_BASE="yes", HAVE_GST_BASE="no")

if test "x$HAVE_GST_BASE" = "xno"; then
  AC_MSG_ERROR(no GStreamer Base Libs found)
fi

AC_SUBST(GST_BASE_LIBS)
AC_SUBST(GST_BASE_CFLAGS)


dnl check for gstreamer core features (subsystems)
GST_CONFIGPATH=`$PKG_CONFIG --variable=includedir gstreamer-0.10`"/gst/gstconfig.h"
AG_GST_PARSE_SUBSYSTEM_DISABLES($GST_CONFIGPATH)
if test $GST_DISABLE_LOADSAVE = "1"; then
  AC_MSG_WARN("Load/Save XML persistence disabled")
  IGNORE_GST_LOADSAVE="gst-disable-loadsave.ignore"
else
  IGNORE_GST_LOADSAVE=""
fi
AC_SUBST(IGNORE_GST_LOADSAVE)

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

dnl check for gstreamer-dataprotocol
PKG_CHECK_MODULES(GST_DP, gstreamer-dataprotocol-$GST_MAJORMINOR >= $GST_REQ,
  HAVE_GST_DP="yes", HAVE_GST_DP="no")

if test "x$HAVE_GST_DP" = "xno"; then
  AC_MSG_ERROR(no GStreamer Data Protocol Libs found)
fi

AC_SUBST(GST_DP_LIBS)
AC_SUBST(GST_DP_CFLAGS)


dnl check for gst-plugins-base
PKG_CHECK_MODULES(GST_PLUGINS_BASE, gstreamer-plugins-base-$GST_MAJORMINOR >= $GSTPB_REQ,
  HAVE_GST_PLUGINS_BASE="yes", HAVE_GST_PLUGINS_BASE="no")

if test "x$HAVE_GST_PLUGINS_BASE" = "xno"; then
  AC_MSG_ERROR(no gst-plugins-base found)
fi

AC_SUBST(GST_PLUGINS_BASE_LIBS)
AC_SUBST(GST_PLUGINS_BASE_CFLAGS)


dnl check for pygobject
PKG_CHECK_MODULES(PYGOBJECT, pygobject-2.0 >= $PYGOBJECT_REQ,
  [
    HAVE_PYGOBJECT="yes"
  ], HAVE_PYGOBJECT="no")

if test "x$HAVE_PYGOBJECT" = "xno"; then
  dnl If we don't have pygobject, then check for pygtk
  dnl check for pygtk
  PKG_CHECK_MODULES(PYGTK, pygtk-2.0 >= $PYGTK_REQ)
  AC_SUBST(PYGTK_CFLAGS)
  PYGOBJECT_CFLAGS="\$(PYGTK_CFLAGS)"
fi
AC_SUBST(PYGOBJECT_CFLAGS)

dnl FIXME: check for a pygobject with a correct pyg_param_gvalue_from_pyobject
PKG_CHECK_MODULES(PYGOBJECT_2_12, pygobject-2.0 >= 2.11.1,
  [
    HAVE_PYGOBJECT_2_12="yes"
    AC_DEFINE_UNQUOTED(HAVE_PYGOBJECT_2_12, 1,
      [Defined if we have a 2.12 series pygobject])
  ], HAVE_PYGOBJECT_2_12="no")

dnl FIXME: check for a pygobject which exports pyg_option_group_new
PKG_CHECK_MODULES(PYGOBJECT_2_16, pygobject-2.0 >= 2.15.0,
  [
    HAVE_PYGOBJECT_2_16="yes"
    AC_DEFINE_UNQUOTED(HAVE_PYGOBJECT_2_16, 1,
      [Defined if we have a 2.16 series pygobject])
  ], HAVE_PYGOBJECT_2_16="no")

AM_CONDITIONAL(HAVE_PYGOBJECT_2_16, test x$HAVE_PYGOBJECT_2_16 != xno)

dnl define an ERROR_CFLAGS Makefile variable
AG_GST_SET_ERROR_CFLAGS($GST_CVS)

GST_OPTION_CFLAGS="\$(ERROR_CFLAGS) \$(GCOV_CFLAGS)"
AC_SUBST(GST_OPTION_CFLAGS)
GST_OPTION_LIBS="\$(GCOV_LIBS)"
AC_SUBST(GST_OPTION_LIBS)

dnl full installation path
AS_AC_EXPAND(PYTHONDIR, $pythondir)
AS_AC_EXPAND(PYEXECDIR, $pyexecdir)

PYGST_CFLAGS="-I\$(top_srcdir)/gst -I\$(top_builddir)/gst"
AC_SUBST(PYGST_CFLAGS)
dnl add debugging options ...
# changequote(,)dnl
# if test "x$GCC" = xyes; then
#   case " $CFLAGS " in
#   *[\	\ ]-Wall[\	\ ]*) ;;
#   *) CFLAGS="$CFLAGS -Wall" ;;
#   esac

#   case " $CFLAGS " in
#   *[\	\ ]-std=c9x[\	\ ]*) ;;
#   *) CFLAGS="$CFLAGS -std=c9x" ;;
#   esac
# fi
# changequote([,])dnl

AG_GST_VALGRIND_CHECK

dnl Stuff needed for the python plugin loader

AM_CHECK_PYTHON_LIBS(,[AC_MSG_ERROR(could not find Python lib)])

AG_GST_SET_PLUGINDIR

AC_OUTPUT([
  Makefile
  codegen/Makefile
  common/Makefile
  common/m4/Makefile
  gst/Makefile
  gst/gstversion.override
  gst/extend/Makefile
  examples/Makefile
  pkgconfig/Makefile
  pkgconfig/gst-python.pc
  pkgconfig/gst-python-uninstalled.pc
  plugin/Makefile
  testsuite/Makefile
  win32/common/config.h
  gst-python.spec
  gst/__init__.py
])
