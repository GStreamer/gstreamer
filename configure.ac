AC_INIT(gstreamer-sharp, 0.9.1.1,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer&component=gst-sharp,
    gstreamer-sharp)
AC_CANONICAL_SYSTEM
AC_CONFIG_SRCDIR([README])
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([-Wno-portability])
AM_MAINTAINER_MODE

AC_PROG_CC
AC_PROG_INSTALL
AC_HEADER_STDC

PACKAGE_VERSION=gstreamer-sharp-0.10
AC_SUBST(PACKAGE_VERSION)

API_VERSION=0.9.2.0
AC_SUBST(API_VERSION)

MONO_REQUIRED_VERSION=2.4
AC_SUBST(MONO_REQUIRED_VERSION)
PKG_CHECK_MODULES(MONO_DEPENDENCY, mono >= $MONO_REQUIRED_VERSION, has_mono=true, has_mono=false)

AM_PROG_LIBTOOL

if test "x$has_mono" = "xtrue"; then
	GACUTIL_FLAGS='/package $(PACKAGE_VERSION) /gacdir $(libdir) /root $(DESTDIR)/$(libdir)'
	GENERATED_SOURCES=generated/*.cs
	AC_PATH_PROG(MONO, mono, no)
	AC_PATH_PROG(CSC, gmcs, no)
	if test `uname -s` = "Darwin"; then
        	LIB_PREFIX=
	        LIB_SUFFIX=.dylib
	else
        	LIB_PREFIX=.so
	        LIB_SUFFIX=
	fi
else
	AC_PATH_PROG(CSC, csc.exe, no)
	GACUTIL_FLAGS=
	GENERATED_SOURCES=generated\\\\*.cs

	if test x$CSC = "xno"; then
        	AC_MSG_ERROR([You need to install either mono or .Net])
	else
		RUNTIME=
		LIB_PREFIX=
		LIB_SUFFIX=.dll
	fi
fi
AM_CONDITIONAL(USE_MONO_COMPILER, test "x$has_mono" = "xtrue")
AC_SUBST(MONO)

CS="C#"
if test "x$CSC" = "xno" ; then
        AC_MSG_ERROR([No $CS compiler found])
fi

AC_PATH_PROG(GACUTIL, gacutil, no)
if test "x$GACUTIL" = "xno" ; then
        AC_MSG_ERROR([No gacutil tool found])
fi

AC_SUBST(RUNTIME)
AC_SUBST(CSC)
AC_SUBST(GACUTIL)
AC_SUBST(GACUTIL_FLAGS)
AC_SUBST(LIB_PREFIX)
AC_SUBST(LIB_SUFFIX)
AC_SUBST(GENERATED_SOURCES)

MONODOC_REQUIRED_VERSION=1.1
AC_SUBST(MONODOC_REQUIRED_VERSION)
PKG_CHECK_MODULES(MONODOC_DEPENDENCY, monodoc >= $MONODOC_REQUIRED_VERSION, enable_monodoc=yes, enable_monodoc=no)

if test "x$enable_monodoc" = "xyes"; then
	AC_PATH_PROG(MONODOCER, monodocer, no)
	AC_PATH_PROG(MDASSEMBLER, mdassembler, no)
	AC_PATH_PROG(MONODOCS2HTML, monodocs2html, no)
	if test x$MONODOCER = xno -o x$MDASSEMBLER = xno -o x$MONODOCS2HTML = xno; then
		enable_monodoc=no
	fi
else
	MONODOCER=
	MDASSEMBLER=
	MONODOCS2HTML=
fi

AC_SUBST(MONODOCER)
AC_SUBST(MDASSEMBLER)
AC_SUBST(MONODOCS2HTML)

## Versions of dependencies
#GLIBSHARP_REQUIRED_VERSION=2.12.9.90
#PKG_CHECK_MODULES(GLIBSHARP, glib-sharp-2.0 >= $GLIBSHARP_REQUIRED_VERSION)
#AC_SUBST(GLIBSHARP_LIBS)

GLIB_REQUIRED_VERSION=2.18.1
PKG_CHECK_MODULES(GLIB,
	glib-2.0 >= $GLIB_REQUIRED_VERSION
	gobject-2.0 >= $GLIB_REQUIRED_VERSION
)
AC_SUBST(GLIB_LIBS)
AC_SUBST(GLIB_CFLAGS)

GSTREAMER_REQUIRED_VERSION=0.10.24
GSTREAMER_PLUGINS_REQUIRED_VERSION=0.10.24
AC_SUBST(GSTREAMER_REQUIRED_VERSION)
AC_SUBST(GSTREAMER_PLUGINS_REQUIRED_VERSION)
PKG_CHECK_MODULES(GST,
	gstreamer-0.10 >= $GSTREAMER_REQUIRED_VERSION
	gstreamer-base-0.10 >= $GSTREAMER_REQUIRED_VERSION
	gstreamer-controller-0.10 >= $GSTREAMER_REQUIRED_VERSION
	gstreamer-app-0.10 >= $GSTREAMER_REQUIRED_VERSION
	gstreamer-audio-0.10 >= $GSTREAMER_REQUIRED_VERSION
	gstreamer-cdda-0.10 >= $GSTREAMER_REQUIRED_VERSION
	gstreamer-interfaces-0.10 >= $GSTREAMER_REQUIRED_VERSION
	gstreamer-pbutils-0.10 >= $GSTREAMER_REQUIRED_VERSION
	gstreamer-tag-0.10 >= $GSTREAMER_REQUIRED_VERSION
	gstreamer-video-0.10 >= $GSTREAMER_REQUIRED_VERSION
)
AC_SUBST(GST_LIBS)
AC_SUBST(GST_CFLAGS)

# glib-sharp and gtk-sharp and mono-cairo are currently only needed for the samples
PKG_CHECK_MODULES(GLIB_SHARP, glib-sharp-2.0 >= 2.12, have_glibsharp=yes, have_glibsharp=no)
PKG_CHECK_MODULES(GTK_SHARP, gtk-sharp-2.0 >= 2.12, have_gtksharp=yes, have_gtksharp=no)
PKG_CHECK_MODULES(GTK_2_17_3, gtk+-2.0 >= 2.17.3, have_gtk_2_17_3=yes, have_gtk_2_17_3=no)
if test "x$have_gtk_2_17_3" = "xyes"; then
    GTK_SHARP_LIBS="$GTK_SHARP_LIBS -define:HAVE_GTK_2_17_3"
fi
PKG_CHECK_MODULES(MONO_CAIRO, mono-cairo, have_monocairo=yes, have_monocairo=no)
AC_SUBST(GLIB_SHARP_LIBS)
AC_SUBST(GTK_SHARP_LIBS)
AC_SUBST(MONO_CAIRO_LIBS)
AM_CONDITIONAL(HAVE_GLIB_SHARP, test "x$have_glibsharp" = "xyes")
AM_CONDITIONAL(HAVE_GTK_SHARP, test "x$have_gtksharp" = "xyes")
AM_CONDITIONAL(HAVE_MONO_CAIRO, test "x$have_monocairo" = "xyes")

AM_CONDITIONAL(ENABLE_MONODOC, test "x$enable_monodoc" = "xyes")

WIN64DEFINES=
case "$host" in
        x86_64-*-mingw*|x86_64-*-cygwin*)
                WIN64DEFINES="-define:WIN64LONGS"
                platform_win32=yes
                AC_DEFINE(PLATFORM_WIN32,1,[Platform is Win32])
                if test "x$cross_compiling" = "xno"; then
                        CC="gcc -mno-cygwin -g"
                        HOST_CC="gcc"
                fi
                ;;
        *-*-mingw*|*-*-cygwin*)
                platform_win32=yes
                AC_DEFINE(PLATFORM_WIN32,1,[Platform is Win32])
                if test "x$cross_compiling" = "xno"; then
                        CC="gcc -mno-cygwin -g"
                        HOST_CC="gcc"
                fi
                ;;
        *)
                platform_win32=no
                ;;
esac
AC_SUBST(WIN64DEFINES)

## Check for the gapi programs
#PKG_CHECK_MODULES(GAPI, gapi-2.0 >= $GLIBSHARP_REQUIRED_VERSION)

## NUnit tests
AC_PATH_PROG(NUNIT_TESTER, nunit-console, no)
if test "x$NUNIT_TESTER" = "xno" ; then
	AC_MSG_WARN([Could not find nunit-console: unit tests cannot be executed])
fi
AC_SUBST(NUNIT_TESTER)

PKG_CHECK_MODULES(MONO_NUNIT, nunit >= 2.4,
    do_tests="yes",
    [ PKG_CHECK_MODULES(MONO_NUNIT, mono-nunit >= 2.4,
        do_tests="yes", do_tests="no")
    ])
AC_SUBST(MONO_NUNIT_LIBS)
AM_CONDITIONAL(ENABLE_TESTS, test "x$do_tests" = "xyes")
if test "x$do_tests" = "xno"; then
	AC_MSG_WARN([Could not find mono-nunit or nunit: tests will not be available])
fi

AC_CHECK_SIZEOF(off_t)
OFF_T_FLAGS="-define:OFF_T_$ac_cv_sizeof_off_t"
AC_SUBST(OFF_T_FLAGS)

AC_OUTPUT([
source/Makefile
parser/Makefile
elementgen/Makefile
generator/Makefile
gstreamer-sharp/Makefile
gstreamer-sharp/AssemblyInfo.cs
gstreamer-sharp/gstreamer-sharp.dll.config
gstreamer-sharp/coreplugins/Makefile
gstreamer-sharp/baseplugins/Makefile
gstreamer-sharp/glue/Makefile
doc/Makefile
tests/Makefile
samples/Makefile
gstreamer-sharp-0.10.pc
gstreamer-sharp-0.10-uninstalled.pc
Makefile
])

echo "---"
echo "Configuration summary"
echo ""
echo "   * Installation prefix = $prefix"
echo "   * $CS compiler: $CSC"
echo "   * Documentation: $enable_monodoc ($MONODOC)"
echo ""
echo "---"

