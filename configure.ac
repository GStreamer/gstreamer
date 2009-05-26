AC_INIT(README)
AC_CANONICAL_SYSTEM
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE(gstreamer-sharp, 0.9.5.99)
AM_MAINTAINER_MODE

AC_PROG_CC
AC_PROG_INSTALL
AC_HEADER_STDC

PACKAGE_VERSION=gstreamer-sharp-0.10
AC_SUBST(PACKAGE_VERSION)

MONO_REQUIRED_VERSION=1.1
AC_SUBST(MONO_REQUIRED_VERSION)
PKG_CHECK_MODULES(MONO_DEPENDENCY, mono >= $MONO_REQUIRED_VERSION, has_mono=true, has_mono=false)

AM_PROG_LIBTOOL

if test "x$has_mono" = "xtrue"; then
	GACUTIL_FLAGS='/package $(PACKAGE_VERSION) /gacdir $(libdir) /root $(DESTDIR)/$(libdir)'
	GENERATED_SOURCES=generated/*.cs
	AC_PATH_PROG(RUNTIME, mono, no)
	AC_PATH_PROG(CSC, mcs, no)
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

MONODOC_SOURCESDIR=`pkg-config --variable=sourcesdir monodoc`
AC_SUBST(MONODOC_SOURCESDIR)

## Versions of dependencies
GLIBSHARP_REQUIRED_VERSION=2.13.90
PKG_CHECK_MODULES(GLIBSHARP, glib-sharp-2.0 >= $GLIBSHARP_REQUIRED_VERSION)
AC_SUBST(GLIBSHARP_LIBS)

GLIB_REQUIRED_VERSION=2.14.0
PKG_CHECK_MODULES(GLIB,
	glib-2.0 >= $GLIB_REQUIRED_VERSION
	gobject-2.0 >= $GLIB_REQUIRED_VERSION
)
AC_SUBST(GLIB_LIBS)
AC_SUBST(GLIB_CFLAGS)

GSTREAMER_REQUIRED_VERSION=0.10.22.1
GSTREAMER_PLUGINS_REQUIRED_VERSION=0.10.22.1
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

AM_CONDITIONAL(ENABLE_MONODOC, test "x$enable_monodoc" = "xyes")

## Check for the gapi programs
PKG_CHECK_MODULES(GAPI, gapi-2.0 >= $GLIBSHARP_REQUIRED_VERSION)

AC_PATH_PROG(GAPI_PARSER, gapi2-parser, no)
if test "x$GAPI_PARSER" = "xno"; then
        AC_MSG_ERROR([You need to install gtk-sharp-gapi])
fi

PKG_CHECK_MODULES(MONO_NUNIT, mono-nunit >= 1.0, do_tests="yes", do_tests="no")
AC_SUBST(MONO_NUNIT_LIBS)
AM_CONDITIONAL(ENABLE_TESTS, test "x$do_tests" = "xyes")
if test "x$do_tests" = "xno"; then
	AC_MSG_WARN([Could not find mono-nunit: tests will not be available])
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

