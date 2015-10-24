AC_PREREQ(2.62)
dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT(Gst-Validate, 1.7.0.1,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gst-validate)

dnl This is the version of the testsuite to use with the current version
dnl of gst-validate. During development it should be "master"
dnl during release cycle it should be the release version (1.6 in the 1.6
dnl branch, 1.5.90 for the 1.5.90 release)
GST_VALIDATE_TESTSUITE_VERSION="master"
AS_AC_EXPAND(GST_VALIDATE_TESTSUITE_VERSION, $GST_VALIDATE_TESTSUITE_VERSION)

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability 1.11 no-dist-gzip dist-xz tar-ustar])

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_GIT="no", GST_GIT="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([tools/gst-validate.c])

dnl define the output header for config
AC_CONFIG_HEADERS([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE([enable])

dnl sets host_* variables
AC_CANONICAL_HOST

dnl use pretty build output with automake >= 1.11
m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])],
  [AM_DEFAULT_VERBOSITY=1
   AC_SUBST(AM_DEFAULT_VERBOSITY)])

dnl our libraries and install dirs use major.minor as a version
dnl GST_API_VERSION=$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_API_VERSION=1.0
AC_SUBST(GST_API_VERSION)
AC_DEFINE_UNQUOTED(GST_API_VERSION, "$GST_API_VERSION",
      [GStreamer API Version])

AS_LIBTOOL(GST, 0, 0, 0)

dnl *** required versions of GStreamer stuff ***
GST_REQ=1.0.0
GSTPB_REQ=1.0.0

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
AG_GST_GETTEXT([gst-validate-$GST_API_VERSION])

dnl Check wether to build LDPRELOAD related code or not
AC_CANONICAL_HOST
case $host_os in
  mingw* | msvc* | mks*)
    BUILD_LDPRELOAD=no ;;
  *)
    BUILD_LDPRELOAD=yes ;;
esac
AM_CONDITIONAL(HAVE_LD_PRELOAD, test "x$BUILD_LDPRELOAD" = "xyes")

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


AC_CHECK_PROG(enable_sphinx_doc, sphinx-build, yes, no)
AM_CONDITIONAL(HAVE_SPHINHX, test ! "x$enable_sphinx_doc" = "xno")

dnl *** checks for libraries ***

dnl *** checks for header files ***

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***

dnl *** checks for dependancy libraries ***

dnl check for libm
LT_LIB_M
AC_SUBST(LIBM)

dnl GLib is required
GLIB_REQ=2.36.0
AC_SUBST([GLIB_REQ])
AG_GST_GLIB_CHECK([$GLIB_REQ])

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_API_VERSION, [$GST_REQ], [yes])

GST_TOOLS_DIR=`$PKG_CONFIG --variable=toolsdir gstreamer-$GST_API_VERSION`
if test -z $GST_TOOLS_DIR; then
  AC_MSG_ERROR([no tools dir defined in GStreamer pkg-config file; core upgrade needed.])
fi
AC_SUBST(GST_TOOLS_DIR)

GST_PLUGINS_DIR=`$PKG_CONFIG gstreamer-$GST_API_VERSION --variable pluginsdir`
AC_SUBST(GST_PLUGINS_DIR)
AC_MSG_NOTICE(Using GStreamer Core Plugins in $GST_PLUGINS_DIR)

AG_GST_CHECK_GST_BASE($GST_API_VERSION, [$GST_REQ], [yes])

AG_GST_CHECK_GST_PLUGINS_BASE($GST_API_VERSION, [$GSTPB_REQ], [yes])
GSTPB_PLUGINS_DIR=`$PKG_CONFIG gstreamer-plugins-base-$GST_API_VERSION --variable pluginsdir`
AC_SUBST(GSTPB_PLUGINS_DIR)
AC_MSG_NOTICE(Using GStreamer Base Plugins in $GSTPB_PLUGINS_DIR)

dnl check for gstreamer-pbutils
PKG_CHECK_MODULES(GST_PBUTILS, gstreamer-pbutils-$GST_API_VERSION, HAVE_GST_PBUTILS="yes", HAVE_GST_PBUTILS="no")
if test "x$HAVE_GST_PBUTILS" != "xyes"; then
  AC_ERROR([gst-pbutils is required])
fi
AC_SUBST(GST_PBUTILS_LIBS)
AC_SUBST(GST_PBUTILS_CFLAGS)

dnl check for gstreamer-video
PKG_CHECK_MODULES(GST_VIDEO, gstreamer-video-$GST_API_VERSION >= 1.4, HAVE_GST_VIDEO="yes", HAVE_GST_VIDEO="no")
if test "x$HAVE_GST_VIDEO" != "xyes"; then
  AC_ERROR([gst-video is required])
fi
AC_SUBST(GST_VIDEO_LIBS)
AC_SUBST(GST_VIDEO_CFLAGS)

dnl needed for scenarios definition files
GST_PREFIX="`$PKG_CONFIG --variable=prefix gstreamer-$GST_API_VERSION`"
AC_SUBST(GST_PREFIX)
GST_DATADIR="$GST_PREFIX/share"
AC_DEFINE_UNQUOTED(GST_DATADIR, "$GST_DATADIR", [system wide data directory])

PKG_CHECK_MODULES(GIO, gio-2.0, HAVE_GIO=yes, HAVE_GIO=no)
AC_SUBST(GIO_CFLAGS)
AC_SUBST(GIO_LIBS)

PKG_CHECK_MODULES(GTK, gtk+-3.0, HAVE_GTK=yes, HAVE_GTK=no)
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)
AM_CONDITIONAL(HAVE_GTK, test "x$HAVE_GTK" = "xyes")

PKG_CHECK_MODULES(GDK, gdk-3.0, HAVE_GDK=yes, HAVE_GDK=no)
AC_SUBST(GDK_CFLAGS)
AC_SUBST(GDK_LIBS)

PKG_CHECK_MODULES(CAIRO, "cairo", HAVE_CAIRO=yes, HAVE_CAIRO=no)
AC_SUBST(CAIRO_CFLAGS)
AC_SUBST(CAIRO_LIBS)
AM_CONDITIONAL(HAVE_CAIRO, test ! "x$HAVE_CAIRO" = "xno")
if test "x$HAVE_CAIRO" != "xyes"; then
  AC_MSG_NOTICE([Cairo is needed for the gst-validate-images-tool])
fi

dnl checks for gstreamer

AG_GST_CHECK_GST_CHECK($GST_API_VERSION, [$GST_REQ], no)
AM_CONDITIONAL(HAVE_GST_CHECK, test "x$HAVE_GST_CHECK" = "xyes")

dnl *** set variables based on configure arguments ***

dnl set license and copyright notice
GST_LICENSE="LGPL"
AC_DEFINE_UNQUOTED(GST_LICENSE, "$GST_LICENSE", [GStreamer license])
AC_SUBST(GST_LICENSE)

dnl define location of plugin directory
AS_AC_EXPAND(VALIDATEPLUGINDIR, ${libdir}/gstreamer-$GST_API_VERSION/validate)
AC_DEFINE_UNQUOTED(VALIDATEPLUGINDIR, "$VALIDATEPLUGINDIR",
[directory where GstValidate plugins are located])
AC_MSG_NOTICE([Using $VALIDATEPLUGINDIR as the plugin install location for GstValidate])

dnl plugin directory configure-time variable for use in Makefile.am
validateplugindir="\$(libdir)/gstreamer-$GST_API_VERSION/validate"
AC_SUBST(validateplugindir)

dnl set location of plugin directory
AG_GST_SET_PLUGINDIR

# set by AG_GST_PARSE_SUBSYSTEM_DISABLES above
dnl make sure it doesn't complain about unused variables if debugging is disabled
NO_WARNINGS=""
AG_GST_CHECK_GST_DEBUG_DISABLED([NO_WARNINGS="-Wno-unused"], [NO_WARNINGS=""])

dnl define an ERROR_CFLAGS Makefile variable
AG_GST_SET_ERROR_CFLAGS($GST_GIT, [-Wmissing-declarations -Wmissing-prototypes -Wredundant-decls -Wundef -Wwrite-strings -Wformat-security -Wold-style-definition -Winit-self -Wmissing-include-dirs -Waddress -Waggregate-return -Wno-multichar -Wnested-externs $NO_WARNINGS])

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
GST_OPTION_CFLAGS="\$(WARNING_CFLAGS) \$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"
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
GST_ALL_CFLAGS="-I\$(top_srcdir) -I\$(top_builddir) $GST_PLUGINS_BASE_CFLAGS $GST_CFLAGS \$(GST_OPTION_CFLAGS)"
AC_SUBST([GST_ALL_CFLAGS])

dnl FIXME: check if LTLIBINTL is needed everywhere
dnl I presume it is given that it contains the symbols that _() stuff maps to
GST_ALL_LIBS="$GST_LIBS $LTLIBINTL \$(GCOV_LIBS)"
AC_SUBST([GST_ALL_LIBS])

dnl LDFLAGS really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_ALL_LDFLAGS="-no-undefined"
AC_SUBST(GST_ALL_LDFLAGS)

dnl GST_LIB_LDFLAGS
dnl linker flags shared by all libraries
dnl LDFLAGS modifier defining exported symbols from built libraries
GST_LIB_LDFLAGS="-export-symbols-regex \^[_]?\(gst_\|Gst\|GST_\).*"
AC_SUBST(GST_LIB_LDFLAGS)

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_.*' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

AM_PATH_PYTHON(2.7.0)
AS_AC_EXPAND(LIBDIR, $libdir)
AC_MSG_NOTICE(Storing library files in $LIBDIR)
AC_CONFIG_FILES([tools/gst-validate-launcher], [chmod +x tools/gst-validate-launcher])
AS_AC_EXPAND(DATADIR, $datadir)
AC_CONFIG_FILES([launcher/config.py])

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking

dnl *** output files ***

dnl keep this alphabetic per directory, please
AC_CONFIG_FILES([
Makefile
common/Makefile
common/m4/Makefile
data/Makefile
data/scenarios/Makefile
gst/Makefile
gst/validate/Makefile
gst/overrides/Makefile
plugins/Makefile
plugins/fault_injection/Makefile
plugins/gapplication/Makefile
plugins/gtk/Makefile
plugins/ssim/Makefile
gst-libs/Makefile
gst-libs/gst/Makefile
gst-libs/gst/video/Makefile
tests/Makefile
tests/check/Makefile
pkgconfig/Makefile
pkgconfig/gst-validate-uninstalled.pc
pkgconfig/gst-validate.pc
po/Makefile.in
tools/Makefile
launcher/Makefile
launcher/apps/Makefile
docs/Makefile
docs/version.entities
docs/validate/Makefile
docs/plugins/Makefile
docs/launcher/Makefile
])
AC_OUTPUT

echo "

Configuration
	Version                    : ${VERSION}
	Source code location       : ${srcdir}
	Prefix                     : ${prefix}
	Compiler                   : ${CC}

gst-validate configured. Type 'make' to build.
"
