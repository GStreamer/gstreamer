# configure.ac

AC_INIT([gst-mediainfo], [0.1])
AC_CONFIG_SRCDIR([src/mi.vala])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([check-news])

# Enable silent rules is available
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_VALAC([0.7.0])
AC_PROG_INSTALL
AC_PROG_INTLTOOL([0.35])
PKG_PROG_PKG_CONFIG

pkg_modules="gee-1.0 gtk+-2.0 >= 2.14.0 gstreamer-0.10 >= 0.10.30 gstreamer-plugins-base-0.10 >= 0.10.30"
MI_PACKAGES="--pkg gee-1.0 --pkg gtk+-2.0 --pkg gdk-x11-2.0 --pkg gstreamer-0.10 --pkg gstreamer-interfaces-0.10 --pkg gstreamer-pbutils-0.10"
PKG_CHECK_MODULES(MI, [$pkg_modules])
AC_SUBST(MI_CFLAGS)
AC_SUBST(MI_LIBS)
AC_SUBST(MI_PACKAGES)

GETTEXT_PACKAGE=gst-mi
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$GETTEXT_PACKAGE"], [Gettext Package])
AC_SUBST(GETTEXT_PACKAGE)
AM_GNU_GETTEXT_VERSION([0.18.1])
AM_GNU_GETTEXT([external])

AC_CONFIG_FILES([
	Makefile
	src/Makefile
	po/Makefile.in
	vapi/Makefile
])
AC_OUTPUT

