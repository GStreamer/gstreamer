AC_PREREQ(2.62)
dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT(Gst-Mediainfo, 1.11.0.1,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gst-mediainfo)
AC_CONFIG_SRCDIR([src/mi.vala])
AC_CONFIG_HEADERS([config.h])

AM_INIT_AUTOMAKE([check-news])

# Enable silent rules is available
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_VALAC([0.7.0])
AC_PROG_INSTALL
IT_PROG_INTLTOOL([0.35])
PKG_PROG_PKG_CONFIG

pkg_modules="gee-0.8 gtk+-3.0 >= 3.0.0 gstreamer-1.0 >= 1.1.0 gstreamer-plugins-base-1.0 >= 1.1.0 gstreamer-pbutils-1.0 >= 1.1.0 gstreamer-video-1.0 >= 1.1.0"
MI_PACKAGES="--pkg gee-0.8 --pkg gtk+-3.0 --pkg gdk-x11-3.0 --pkg gstreamer-1.0 --pkg gstreamer-pbutils-1.0 --pkg gstreamer-video-1.0"
PKG_CHECK_MODULES([MI], [$pkg_modules])
AC_SUBST(MI_CFLAGS)
AC_SUBST(MI_LIBS)
AC_SUBST(MI_PACKAGES)

GETTEXT_PACKAGE=gst-mi
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Gettext Package])
AC_SUBST(GETTEXT_PACKAGE)
# AC_PROG_INTLTOOL is enough, the lines below caused:
#   required file `./config.rpath' not found
#AM_GNU_GETTEXT_VERSION([0.18.1])
#AM_GNU_GETTEXT([external])

dnl check for desktop utilities
AC_PATH_PROG(UPDATE_DESKTOP_DATABASE, update-desktop-database)


AC_CONFIG_FILES([
	Makefile
	src/Makefile
	po/Makefile.in
	vapi/Makefile
])
AC_OUTPUT

