AC_PREREQ(2.52)

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT(GStreamer FFMpeg, 0.10.1.1,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gst-ffmpeg)

dnl initialize automake
AM_INIT_AUTOMAKE

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_CVS="no", GST_CVS="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([ext/ffmpeg/gstffmpeg.c])

dnl define the output header for config
AM_CONFIG_HEADER([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE

dnl sets host_* variables
AC_CANONICAL_HOST

dnl our libraries and install dirs use major.minor as a version
GST_MAJORMINOR=$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_MAJORMINOR=0.10
AC_SUBST(GST_MAJORMINOR)

AM_PROG_LIBTOOL

dnl *** required versions of GStreamer stuff ***
GST_REQ=0.10.4

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl *** check for arguments to configure ***

GST_ARG_WITH_PACKAGE_NAME
GST_ARG_WITH_PACKAGE_ORIGIN

dnl *** checks for platform ***

dnl * hardware/architecture *

dnl common/m4/gst-arch.m4
dnl check CPU type
GST_ARCH

dnl *** checks for programs ***

dnl find a compiler
AC_PROG_CC

# FIXME: are these two lines needed ?
AM_PROG_CC_STDC
AS="${CC}"

dnl check for documentation tools
GTK_DOC_CHECK([1.3])
AS_PATH_PYTHON([2.1])

dnl *** checks for libraries ***

dnl *** checks for header files ***

dnl check if we have ANSI C header files
AC_HEADER_STDC

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***

dnl *** checks for dependancy libraries ***

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
GST_CHECK_GST($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_BASE($GST_MAJORMINOR, [$GST_REQ])
GST_CHECK_GST_PLUGINS_BASE($GST_MAJORMINOR, [$GST_REQ])

dnl *** set variables based on configure arguments ***

dnl set location of plugin directory
GST_SET_PLUGINDIR

dnl define an ERROR_CFLAGS Makefile variable
GST_SET_ERROR_CFLAGS($GST_CVS)

dnl define correct level for debugging messages
GST_SET_LEVEL_DEFAULT($GST_CVS)

dnl *** finalize CFLAGS, LDFLAGS, LIBS ***

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
dnl also add builddir include for enumtypes and marshal
dnl add GST_OPTION_CFLAGS, but overridable
GST_CFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs $GST_CFLAGS \$(GST_OPTION_CFLAGS)"
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_desc\$\$' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl *** configure external libs ***

AC_DEFINE_UNQUOTED(HAVE_FFMPEG_UNINSTALLED, 1,
                   [Defined if building against uninstalled FFmpeg source])
ac_configure_args="$ac_configure_args --disable-v4l --disable-audio-oss --disable-dv1394 --disable-vhook --disable-ffmpeg --disable-ffserver --enable-pp"
AC_CONFIG_SUBDIRS(gst-libs/ext/ffmpeg)

AC_CONFIG_FILES(
Makefile
gst-ffmpeg.spec
ext/Makefile
ext/ffmpeg/Makefile
ext/libpostproc/Makefile
gst-libs/Makefile
gst-libs/ext/Makefile
docs/Makefile
docs/version.entities
docs/plugins/Makefile
common/Makefile
common/m4/Makefile
)
AC_OUTPUT
