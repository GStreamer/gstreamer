AC_PREREQ([2.66])
AC_INIT([codecanalyzer], [0.1.0],
    [sreerenj.balachandran@intel.com])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([build-aux])

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([1.11 no-dist-gzip dist-xz tar-ustar])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

dnl Check for tools
AC_PROG_CC
AM_PROG_CC_C_O

dnl Initialize libtool
LT_PREREQ([2.2])
LT_INIT

# codecanalyzer packag versions
m4_define([codecanalyzer_major_version], [0])
m4_define([codecanalyzer_minor_version], [1])
m4_define([codecanalyzer_build_version], [0])

dnl Check for glib
PKG_CHECK_MODULES([GLIB], [glib-2.0 >= 2.32.0])
PKG_CHECK_MODULES([GMODULE_EXPORT], [gmodule-2.0 >= 2.32.0])
PKG_CHECK_MODULES([GTK], [gtk+-3.0 >= 3.4.2])
PKG_CHECK_MODULES([GST], [gstreamer-1.0 >= 1.3.1])
PKG_CHECK_MODULES([GST_BASE], [gstreamer-base-1.0 >= 1.3.1])
PKG_CHECK_MODULES([GST_PLUGINS_BASE], [gstreamer-plugins-base-1.0 >= 1.3.1])
PKG_CHECK_MODULES([GST_VIDEO], [gstreamer-video-1.0 >= 1.3.1])
PKG_CHECK_MODULES([GST_PBUTILS], [gstreamer-pbutils-1.0 >= 1.3.1])
PKG_CHECK_MODULES([GST_CODEC_PARSERS], [gstreamer-plugins-bad-1.0 >= 1.3.1])
PKG_CHECK_MODULES([LIBXML2], [libxml-2.0 >= 2.7.8])

GST_ALL_LDFLAGS="-no-undefined"
AC_SUBST(GST_ALL_LDFLAGS)

AC_SUBST(GLIB_CFLAGS)
AC_SUBST(GLIB_LIBS)
AC_SUBST(GMODULE_EXPORT_CFLAGS)
AC_SUBST(GMODULE_EXPORT_LIBS)
AC_SUBST(GTK_CFLAGS)
AC_SUBST(GTK_LIBS)
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)
AC_SUBST(GST_BASE_CFLAGS)
AC_SUBST(GST_BASE_LIBS)
AC_SUBST(GST_PLUGINS_CFLAGS)
AC_SUBST(GST_PLUGINS_LIBS)
AC_SUBST(GST_VIDEO_CFLAGS)
AC_SUBST(GST_VIDEO_LIBS)
AC_SUBST(GST_PBUTILS_CFLAGS)
AC_SUBST(GST_PBUTILS_LIBS)
AC_SUBST(GST_CODEC_PARSERS_LIBS)
AC_SUBST(GST_CODEC_PARSERS_CFLAGS)
AC_SUBST(LIBXML2_CFLAGS)
AC_SUBST(LIBXML2_LIBS)

AC_CONFIG_FILES([Makefile
	src/Makefile
	src/plugins/Makefile
	src/plugins/gst/Makefile
	src/plugins/gst/analyzersink/Makefile
	data/Makefile
	data/ui/Makefile
	data/pixmaps/Makefile
	])
AC_OUTPUT

echo
echo $PACKAGE $VERSION
echo " configure complete, now type make and make install "
echo " ===================================================== "
