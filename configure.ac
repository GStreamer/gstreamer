AC_PREREQ(2.60)
dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT(Gst-RTSP, 0.11.0.1,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gst-rtsp)
AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability 1.10])

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_GIT="no", GST_GIT="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([gst/rtsp-server/rtsp-server.c])

dnl define the output header for config
AM_CONFIG_HEADER([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl sets host_* variables
AC_CANONICAL_HOST

dnl use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST(AM_DEFAULT_VERBOSITY)])

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_MAJORMINOR=0.11
AC_SUBST(GST_MAJORMINOR)

AM_PROG_LIBTOOL

dnl *** required versions of GStreamer stuff ***
GST_REQ=0.11.0
GSTPB_REQ=0.11.0

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")
AC_CONFIG_MACRO_DIR([m4])

dnl set up gettext
dnl the version check needs to stay here because autopoint greps for it
AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])
AG_GST_GETTEXT([gstreamer-$GST_MAJORMINOR])

dnl *** check for arguments to configure ***

AG_GST_ARG_DEBUG
AG_GST_ARG_VALGRIND
AG_GST_ARG_GCOV
AG_GST_ARG_WITH_PACKAGE_NAME
AG_GST_ARG_WITH_PACKAGE_ORIGIN

AG_GST_PKG_CONFIG_PATH

dnl *** checks for platform ***

dnl * hardware/architecture *

dnl *** checks for programs ***

dnl find a compiler
AC_PROG_CC
AM_PROG_CC_C_O

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for gobject-introspection
GOBJECT_INTROSPECTION_CHECK([0.6.3])

dnl check for documentation tools
AG_GST_DOCBOOK_CHECK
GTK_DOC_CHECK([1.3])

dnl check for python
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
  HAVE_PYTHON=yes
  AC_MSG_RESULT(okay)
else
  HAVE_PYTHON=no
  AC_MSG_RESULT(no python)
fi

AM_CHECK_PYTHON_HEADERS([HAVE_PYTHON_HEADERS=yes],[HAVE_PYTHON_HEADERS=no])

dnl check for pygobject (optional, used in the bindings)
PYGOBJECT_REQ=2.11.2
PKG_CHECK_MODULES(PYGOBJECT, pygobject-2.0 >= $PYGOBJECT_REQ,
  [HAVE_PYGOBJECT="yes"], [HAVE_PYGOBJECT="no"])
AC_SUBST(PYGOBJECT_CFLAGS)

dnl check for gst-python
PKG_CHECK_MODULES(PYGST, gst-python-0.10,
  [HAVE_PYGST="yes"], [HAVE_PYGST="no"])

if test "x$HAVE_PYGST" = "xyes"; then
  PYGST_DEFSDIR=`pkg-config gst-python-0.10 --variable=defsdir`
fi
AC_SUBST(PYGST_DEFSDIR, $PYGST_DEFSDIR)

if test \
    "x$HAVE_PYTHON" = "xyes" -a \
    "x$HAVE_PYTHON_HEADERS" = "xyes" -a \
    "x$HAVE_PYGOBJECT" = "xyes" -a \
    "x$HAVE_PYGST" = "xyes"; then
  HAVE_PYTHON_BINDINGS="yes"
else
  HAVE_PYTHON_BINDINGS="no"
fi

AM_CONDITIONAL(WITH_PYTHON, [test "x$HAVE_PYTHON_BINDINGS" = "xyes"])

dnl Check for Vala
AC_ARG_ENABLE([vala],
  AC_HELP_STRING([--enable-vala],[enable Vala bindings (default=yes)]),
  [case "${enableval}" in
    yes) enable_vala=yes ;;
    no)  enable_vala=no ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-vala) ;;
   esac
  ],
  [enable_vala=yes]) dnl Default value
AM_CONDITIONAL(WITH_VALA, [test "x$enable_vala" = "xyes"])

dnl *** checks for libraries ***

dnl *** checks for header files ***

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***

dnl *** checks for dependancy libraries ***

dnl GLib is required (GStreamer is ok with GLib-2.8, but we want at least 2.10)
GLIB_REQ=2.10.0
AC_SUBST([GLIB_REQ])
AG_GST_GLIB_CHECK([$GLIB_REQ])

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_MAJORMINOR, [$GST_REQ], [yes])

GST_TOOLS_DIR=`$PKG_CONFIG --variable=toolsdir gstreamer-$GST_MAJORMINOR`
if test -z $GST_TOOLS_DIR; then
  AC_MSG_ERROR([no tools dir defined in GStreamer pkg-config file; core upgrade needed.])
fi
AC_SUBST(GST_TOOLS_DIR)

GST_PLUGINS_DIR=`$PKG_CONFIG gstreamer-$GST_MAJORMINOR --variable pluginsdir`
AC_SUBST(GST_PLUGINS_DIR)
AC_MSG_NOTICE(Using GStreamer Core Plugins in $GST_PLUGINS_DIR)

AG_GST_CHECK_GST_BASE($GST_MAJORMINOR, [$GST_REQ], [yes])

AG_GST_CHECK_GST_PLUGINS_BASE($GST_MAJORMINOR, [$GSTPB_REQ], [yes])
GSTPB_PLUGINS_DIR=`$PKG_CONFIG gstreamer-plugins-base-$GST_MAJORMINOR --variable pluginsdir`
AC_SUBST(GSTPB_PLUGINS_DIR)
AC_MSG_NOTICE(Using GStreamer Base Plugins in $GSTPB_PLUGINS_DIR)

AG_GST_CHECK_GST_CHECK($GST_MAJORMINOR, [$GST_REQ], no)

dnl FIXME: get rid of this by making sure gstreamer-check brings it in
dnl check for "check", unit testing library/header
AM_PATH_CHECK(0.9.2, HAVE_CHECK=yes, HAVE_CHECK=no)
AM_CONDITIONAL(HAVE_CHECK, test "x$HAVE_CHECK" = "xyes")

dnl *** set variables based on configure arguments ***

dnl set license and copyright notice
GST_LICENSE="LGPL"
AC_DEFINE_UNQUOTED(GST_LICENSE, "$GST_LICENSE", [GStreamer license])
AC_SUBST(GST_LICENSE)

dnl set location of plugin directory
AG_GST_SET_PLUGINDIR

dnl define an ERROR_CFLAGS Makefile variable
AG_GST_SET_ERROR_CFLAGS($GST_GIT)

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT($GST_GIT)

dnl used in examples
AG_GST_DEFAULT_ELEMENTS

dnl *** finalize CFLAGS, LDFLAGS, LIBS

dnl Overview:
dnl GST_OPTION_CFLAGS:  common flags for profiling, debugging, errors, ...
dnl GST_*:              flags shared by built objects to link against GStreamer
dnl GST_ALL_LDFLAGS:    linker flags shared by all
dnl GST_LIB_LDFLAGS:    additional linker flags for all libaries
dnl GST_LT_LDFLAGS:     library versioning of our libraries
dnl GST_PLUGIN_LDFLAGS: flags to be used for all plugins

dnl GST_OPTION_CFLAGS
if test "x$USE_DEBUG" = xyes; then
   PROFILE_CFLAGS="-g"
fi
AC_SUBST(PROFILE_CFLAGS)

DEPRECATED_CFLAGS="-DGST_DISABLE_DEPRECATED"
AC_SUBST(DEPRECATED_CFLAGS)

dnl every flag in GST_OPTION_CFLAGS can be overridden at make time
GST_OPTION_CFLAGS="\$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
AC_SUBST(GST_OPTION_CFLAGS)

dnl FIXME: do we want to rename to GST_ALL_* ?
dnl prefer internal headers to already installed ones
dnl add GST_OPTION_CFLAGS, but overridable
GST_CFLAGS="$GST_CFLAGS \$(GST_OPTION_CFLAGS)"
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

dnl GST_ALL_*
dnl vars common to for all internal objects (core libs, elements, applications)
dnl CFLAGS:
dnl - src and build dirs need to be added because every piece that gets built
dnl   will need the GStreamer source and generated headers
GST_ALL_CFLAGS="-I\$(top_srcdir) -I\$(top_builddir) $GST_CFLAGS \$(GST_OPTION_CFLAGS)"
AC_SUBST([GST_ALL_CFLAGS])

dnl FIXME: check if LTLIBINTL is needed everywhere
dnl I presume it is given that it contains the symbols that _() stuff maps to
GST_ALL_LIBS="$GST_LIBS $LTLIBINTL \$(GCOV_LIBS)"
AC_SUBST([GST_ALL_LIBS])

dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_ALL_LDFLAGS="-no-undefined"
AC_SUBST(GST_ALL_LDFLAGS)

dnl GST_OBJ_*
dnl default vars for all internal objects built on libgstphonon
dnl includes GST_ALL_*
GST_OBJ_CFLAGS="\$(GST_ALL_CFLAGS)"
AC_SUBST([GST_OBJ_CFLAGS])
GST_OBJ_LIBS="\$(top_builddir)/gst-phonon/libgstphonon.la \$(GST_ALL_LIBS)"
AC_SUBST([GST_OBJ_LIBS])

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking

dnl *** output files ***

dnl keep this alphabetic per directory, please
AC_CONFIG_FILES([
Makefile
gst-rtsp.spec
common/Makefile
common/m4/Makefile
gst/Makefile
gst/rtsp-server/Makefile
examples/Makefile
tests/Makefile
bindings/Makefile
bindings/python/Makefile
bindings/python/codegen/Makefile
bindings/vala/Makefile
pkgconfig/Makefile
pkgconfig/gst-rtsp-server.pc
pkgconfig/gst-rtsp-server-uninstalled.pc
docs/Makefile
docs/version.entities
docs/libs/Makefile
])
AC_OUTPUT

echo "

Configuration
	Version                    : ${VERSION}
	Source code location       : ${srcdir}
	Prefix                     : ${prefix}
	Compiler                   : ${CC}
	Vala bindings              : ${enable_vala}
	Python bindings:           : ${HAVE_PYTHON_BINDINGS}

Gst-rtsp-server configured. Type 'make' to build.
"
