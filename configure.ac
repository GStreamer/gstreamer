dnl Note: this file is now the main autoconf file
AC_INIT
AC_CONFIG_SRCDIR([gst/gstobject.h])

AC_CANONICAL_TARGET([])

AM_CONFIG_HEADER(config.h)

GST_VERSION_MAJOR=0
GST_VERSION_MINOR=3
GST_VERSION_MICRO=01

dnl we use the release tag during CVS development
GST_VERSION_RELEASE=`date +%Y%m%d`

GST_VERSION=$GST_VERSION_MAJOR.$GST_VERSION_MINOR.$GST_VERSION_MICRO

PACKAGE=gstreamer
VERSION=$GST_VERSION

AC_DEFINE_UNQUOTED(PACKAGE, "$PACKAGE")
AC_DEFINE_UNQUOTED(VERSION, "$VERSION")
AC_SUBST(PACKAGE)
AC_SUBST(VERSION)
AC_DEFINE_UNQUOTED(GST_VERSION_RELEASE, "$GST_VERSION_RELEASE")
AC_SUBST(GST_VERSION_RELEASE)

dnl libtool
GST_CURRENT=1
GST_REVISION=0
GST_AGE=0
GST_LIBVERSION=$GST_CURRENT:$GST_REVISION:$GST_AGE

AM_INIT_AUTOMAKE($PACKAGE, $VERSION)
dnl Add parameters for aclocal
dnl (This must come after AM_INIT_AUTOMAKE, since it modifies ACLOCAL)
ACLOCAL="$ACLOCAL $ACLOCAL_FLAGS"

AC_SUBST(GST_VERSION_MAJOR)
AC_SUBST(GST_VERSION_MINOR)
AC_SUBST(GST_VERSION_MICRO)
AC_SUBST(GST_VERSION)

AC_SUBST(GST_CURRENT)
AC_SUBST(GST_REVISION)
AC_SUBST(GST_AGE)
AC_SUBST(GST_LIBVERSION)

AM_MAINTAINER_MODE

AC_PROG_CC
AM_PROG_CC_STDC
AM_PROG_AS
AS="${CC}"
AC_PROG_CXX
AC_PROG_CXXCPP
AC_ISC_POSIX

dnl We disable static building for development, for time savings
dnl *NOTE*: dnl this line before release, so release does static too
AM_DISABLE_STATIC
AC_LIBTOOL_DLOPEN
AM_PROG_LIBTOOL

AC_HEADER_STDC([])


dnl ##############################
dnl # Do automated configuration #
dnl ##############################

dnl Check for tools:
dnl ================

dnl modify pkg-config path
AC_ARG_WITH(pkg-config-path, 
   AC_HELP_STRING([--with-pkg-config-path],[colon-separated list of pkg-config(1) dirs]),
   [export PKG_CONFIG_PATH=${withval}])

dnl Check for nasm
AC_PATH_PROG(NASM_PATH, nasm, no)
AC_SUBST(NASM_PATH)
if test x$NASM_PATH = xno; then
  AC_MSG_WARN(Couldn't find nasm)
  HAVE_NASM="no"
else AC_DEFINE(HAVE_NASM, 1, [Define if NASM, the netwide assembler, is available])
  HAVE_NASM="yes"
fi

dnl check for gtk-doc
AC_ARG_WITH(html-dir, [  --with-html-dir=PATH path to installed docs ])

if test "x$with_html_dir" = "x" ; then
  HTML_DIR='${datadir}/gst/html'
else
  HTML_DIR=$with_html_dir
fi

AC_SUBST(HTML_DIR)

AC_CHECK_PROG(HAVE_GTK_DOC, gtkdoc-mkdb, true, false)
gtk_doc_min_version=0.6
if $HAVE_GTK_DOC ; then 
    gtk_doc_version=`gtkdoc-mkdb --version`
    AC_MSG_CHECKING([gtk-doc version ($gtk_doc_version) >= $gtk_doc_min_version])
    if perl <<EOF ; then
      exit (("$gtk_doc_version" =~ /^[[0-9]]+\.[[0-9]]+$/) &&
            ("$gtk_doc_version" >= "$gtk_doc_min_version") ? 0 : 1);
EOF
      AC_MSG_RESULT(yes)
   else
      AC_MSG_RESULT(no)
      HAVE_GTK_DOC=false
   fi
fi
GTK_DOC_SCANOBJ=gtkdoc-scanobj

AC_SUBST(HAVE_GTK_DOC)
AC_SUBST(GTK_DOC_SCANOBJ)

dnl check for docbook tools
AC_CHECK_PROG(HAVE_XSLTPROC, xsltproc, true, false)
AC_CHECK_PROG(HAVE_PDFTOPS, pdftops, true, false)
dnl this does not yet work properly, at least on debain -- wingo
HAVE_PDFXMLTEX=false

dnl check for image conversion tool
AC_CHECK_PROG(HAVE_FIG2DEV, fig2dev, true, false)

dnl The following is a hack: if fig2dev doesn't display an error message
dnl for the desired type, we assume it supports it.
HAVE_FIG2DEV_PNG=false
if test "x$HAVE_FIG2DEV" = "xtrue" ; then
  fig2dev_quiet=`fig2dev -L png </dev/null 2>&1 >/dev/null`
  if test "x$fig2dev_quiet" = "x" ; then
    HAVE_FIG2DEV_PNG=true
  fi
fi
HAVE_FIG2DEV_PDF=false
if test "x$HAVE_FIG2DEV" = "xtrue" ; then
  fig2dev_quiet=`fig2dev -L pdf </dev/null 2>&1 >/dev/null`
  if test "x$fig2dev_quiet" = "x" ; then
    HAVE_FIG2DEV_PDF=true
  fi
fi


dnl Set up conditionals for (target) architecture:
dnl ==============================================

dnl Determine CPU
case "x${target_cpu}" in
  xi?86 | k?) HAVE_CPU_I386=yes
              AC_DEFINE(HAVE_CPU_I386, 1, [Define if the target CPU is an x86])
              dnl FIXME could use some better detection
              dnl       (ie CPUID)
              case "x${target_cpu}" in
                xi386 | xi486) ;;
                *)             AC_DEFINE(HAVE_RDTSC) ;;
              esac ;;
  xpowerpc)   HAVE_CPU_PPC=yes
              AC_DEFINE(HAVE_CPU_PPC, 1, [Define if the target CPU is a PPC]) ;;
  xalpha)     HAVE_CPU_ALPHA=yes
              AC_DEFINE(HAVE_CPU_ALPHA, 1, [Define if the target CPU is an Alpha]) ;;
  xarm*)      HAVE_CPU_ARM=yes
              AC_DEFINE(HAVE_CPU_ARM, 1, [Define if the target CPU is an ARM]) ;;
  xsparc*)    HAVE_CPU_SPARC=yes
              AC_DEFINE(HAVE_CPU_SPARC, 1, [Define if the target CPU is a PPC]) ;;
  xmips*)     HAVE_CPU_MIPS=yes
              AC_DEFINE(HAVE_CPU_MIPS, 1, [Define if the target CPU is a MIPS]) ;;
  xhppa*)     HAVE_CPU_HPPA=yes
              AC_DEFINE(HAVE_CPU_HPPA, 1, [Define if the target CPU is a HPPA]) ;;
esac

dnl Determine endianness
AC_C_BIGENDIAN

dnl Check for MMX-capable compiler
AC_MSG_CHECKING(for MMX-capable compiler)
AC_TRY_RUN([
#include "include/mmx.h"

main()
{ movq_r2r(mm0, mm1); return 0; }
],
[
HAVE_LIBMMX="yes"
AC_MSG_RESULT(yes)
],
HAVE_LIBMMX="no"
AC_MSG_RESULT(no)
,
HAVE_LIBMMX="no"
AC_MSG_RESULT(no)
)

dnl
dnl We should really use AC_SYS_LARGEFILE, but the problem is
dnl many of the plugins don't include "config.h".  To assure
dnl binary compatibility, it is necessary that all gstreamer
dnl code be compiled with the same sizeof(off_t), so we use
dnl the following crude hack.
dnl

AC_MSG_CHECKING(for large file support)
AC_TRY_RUN([
#define _LARGEFILE_SOURCE
#define _FILE_OFFSET_BITS 64
#include <sys/types.h>
int main () { return !(sizeof(off_t) == 8); }
],
[
AC_MSG_RESULT(yes)
GST_CFLAGS="$GST_CFLAGS -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64"
],
[
AC_MSG_RESULT(no)
],
[
AC_MSG_RESULT(no)
])

dnl Check for essential libraries first:
dnl ====================================

dnl Check for glib2
translit(dnm, m, l) AM_CONDITIONAL(USE_GLIB2, true)
GST_CHECK_FEATURE(GLIB2, [use of glib-2.0 and GObject], , [
  PKG_CHECK_MODULES(GLIB2, glib-2.0 gobject-2.0 gthread-2.0 gmodule-2.0,
    HAVE_GLIB2=yes,HAVE_GLIB2=no)
  GLIB_LIBS=$GLIB2_LIBS
  GLIB_CFLAGS=$GLIB2_CFLAGS
  AC_SUBST(GLIB_LIBS)
  AC_SUBST(GLIB_CFLAGS)
], disabled, [
  AC_DEFINE(USE_GLIB2)
  GST_CFLAGS="$GST_CFLAGS -DUSE_GLIB2"
])
AC_SUBST(USE_GLIB2)

if test x$USE_GLIB2 = xno; then
  GST_PKG_DEPS="glib >= 1.2.0, gtk+ >= 1.2.0"
  
  dnl Check for glib and gtk
  AM_PATH_GLIB(1.2.0,,
              AC_MSG_ERROR(Cannot find glib: Is glib-config in path?),
              glib gmodule gthread)
  AM_PATH_GTK(1.2.0,,
              AC_MSG_ERROR(Cannot find gtk: Is gtk-config in path?))
  HAVE_GTK=yes

  GLIB_LIBS="$GLIB_LIBS $GTK_LIBS"
  GLIB_CFLAGS="$GLIB_CFLAGS $GTK_CFLAGS"
else
  GST_PKG_DEPS="glib-2.0, gobject-2.0, gmodule-2.0, gthread-2.0"
  
  PKG_CHECK_MODULES(GTK2, gtk+-2.0, HAVE_GTK=yes, HAVE_GTK=no)

  GTK_CFLAGS=$GTK2_CFLAGS
  GTK_LIBS=$GTK2_LIBS
  
  if $HAVE_GTK_DOC; then  
    AC_CHECK_PROG(HAVE_GTK_DOC, gtkdoc-scangobj, true, false)
    if $HAVE_GTK_DOC; then
      GTK_DOC_SCANOBJ=gtkdoc-scangobj
    fi;
  fi

  dnl FIXME: check for  gnome2 - this conditional is currently
  dnl always false.
  AM_CONDITIONAL(USE_GNOME2, test "x$USE_GNOME2" = "xyes")
  AC_SUBST(USE_GNOME2)
fi

AC_SUBST(GST_PKG_DEPS)
AC_SUBST(GTK_LIBS)
AC_SUBST(GTK_CFLAGS)

dnl Check for libxml
if test x$USE_GLIB2 = xno; then
  LIBXML_PKG=', libxml >= 1.8.1'
  AC_PATH_PROG(XML_CONFIG, xml-config, no)
  if test x$XML_CONFIG = xno; then
    AC_MSG_ERROR(Couldn't find $LIBXML_PKG)
  fi
  XML_LIBS="`xml-config --libs`"
  XML_CFLAGS="`xml-config --cflags`"
  AC_CHECK_LIB(xml, xmlDocGetRootElement, :,
    [ AC_MSG_ERROR(Need version 1.8.1 or better of libxml) ],
    $XML_LIBS)
else
  LIBXML_PKG=', libxml-2.0'
  PKG_CHECK_MODULES(XML, $LIBXML_PKG, XML_CONFIG=yes, XML_CONFIG=no)
  AC_PATH_PROG(XML_CONFIG, xml-config, no)
  AC_DEFINE(HAVE_LIBXML2)
  GST_CFLAGS="$GST_CFLAGS -DHAVE_LIBXML2"
fi
AC_SUBST(LIBXML_PKG)
AC_SUBST(XML_LIBS)
AC_SUBST(XML_CFLAGS)

if test x$USE_GLIB2 = xyes; then
  dnl we don't support gnome/gtk with glib2.0 yet
  GNOME_LIBS=
  GNOME_CFLAGS=
  HAVE_GNOME=no
  AC_MSG_WARN(gnome disabled for glib2.0)
else
  dnl Check for libgnome
  GST_CHECK_CONFIGPROG(GNOME, gnome-config, gnome gnomeui)
fi
AC_SUBST(GNOME_LIBS)
AC_SUBST(GNOME_CFLAGS)
AC_SUBST(HAVE_GNOME)

dnl Check for libglade with gnome support; no libglade for glib2.0
HAVE_LIBGLADE_GNOME="no"
if test x$USE_GLIB2 = xyes; then
  AC_MSG_WARN(libglade disabled for glib2.0)
  PKG_CHECK_MODULES(LIBGLADE_GNOME, libglade-2.0 libgnomeui-2.0, HAVE_LIBGLADE_GNOME=yes, HAVE_LIBGLADE_GNOME=no)
  if test x$HAVE_LIBGLADE_GNOME = xno; then
    AC_MSG_WARN(Couldn't find libglade-2.0 - Can't build gstplay)
    LIBGLADE_GNOME_LIBS=
    LIBGLADE_GNOME_CFLAGS=
  fi;
else
  AM_PATH_LIBGLADE(HAVE_LIBGLADE_GNOME="yes", HAVE_LIBGLADE_GNOME="no", gnome)
  if test x$HAVE_LIBGLADE_GNOME = xno; then
    AC_MSG_WARN(Couldn't find libglade-config - Can't build gstplay)
  else
    LIBGLADE_GNOME_LIBS=$LIBGLADE_LIBS
    LIBGLADE_GNOME_CFLAGS=$LIBGLADE_CFLAGS
        AC_TRY_LINK([#include <glade/glade.h>],[glade_gnome_init();],
                HAVE_LIBGLADE_GNOME="yes"
                AC_MSG_WARN(
        [Couldn't find gnome libraries for libglade - Can't build gstmediaplay and gsteditor])
               )
  fi
fi
AC_SUBST(HAVE_LIBGLADE_GNOME)
AC_SUBST(LIBGLADE_GNOME_LIBS)
AC_SUBST(LIBGLADE_GNOME_CFLAGS)

dnl Check for atomic.h
dnl Note: use AC_CHECK_HEADER not AC_CHECK_HEADERS, because the latter
dnl defines the wrong default symbol as well (HAVE_ASM_ATOMIC_H)
AC_CHECK_HEADER(asm/atomic.h, HAVE_ATOMIC_H=yes, HAVE_ATOMIC_H=no)
dnl Do a compile to check that it has atomic_set (eg, linux 2.0 didn't)
if test x$HAVE_ATOMIC_H = xyes; then
  AC_TRY_RUN([
#include "asm/atomic.h"
main() { atomic_t t; atomic_set(&t,0); atomic_inc(&t); atomic_add(1,&t);return 0;}
  ],, [
    # Not successful
    if test x$HAVE_ATOMIC_H = xyes; then
      AC_MSG_WARN(Atomic reference counting is out of date: doing without.)
    fi
    HAVE_ATOMIC_H=no
  ], [
    # Cross compiling
    AC_MSG_RESULT(yes)
    AC_MSG_WARN(Can't check properly for atomic reference counting.  Assuming OK.)
  ])
fi


dnl ######################################################################
dnl # Check command line parameters, and set shell variables accordingly #
dnl ######################################################################

AC_ARG_ENABLE(libmmx,
[  --enable-libmmx              use libmmx, if available],
[case "${enableval}" in
  yes) USE_LIBMMX=$HAVE_LIBMMX ;;
  no)  USE_LIBMMX=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-libmmx) ;;
esac], 
[USE_LIBMMX=$HAVE_LIBMMX]) dnl Default value

AC_ARG_ENABLE(atomic,
[  --enable-atomic              use atomic reference counting header],
[case "${enableval}" in
  yes) USE_ATOMIC_H=$HAVE_ATOMIC_H;;
  noset) USE_ATOMIC_H=$HAVE_ATOMIC_H;;
  no)  USE_ATOMIC_H=no;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-atomic) ;;
esac], 
[USE_ATOMIC_H=$HAVE_ATOMIC_H]) dnl Default value

AC_ARG_ENABLE(plugin-builddir,
[  --enable-plugin-builddir     allow tests/demos to use non-installed plugins ],
[case "${enableval}" in
  yes) PLUGINS_USE_BUILDDIR=yes ;;
  no)  PLUGINS_USE_BUILDDIR=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-plugin-builddir) ;;
esac], 
[PLUGINS_USE_BUILDDIR=no]) dnl Default value

AC_ARG_ENABLE(debug,
[  --enable-debug               compile with -g debugging info],
[case "${enableval}" in
  yes) USE_DEBUG=yes ;;
  no)  USE_DEBUG=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
esac], 
[USE_DEBUG=no]) dnl Default value

AC_ARG_ENABLE(DEBUG,
[  --enable-DEBUG               compiles in a large number of debugging messages],
[case "${enableval}" in
  yes) ENABLE_DEBUG=yes ;;
  no)  ENABLE_DEBUG=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-DEBUG) ;;
esac], 
[ENABLE_DEBUG=no]) dnl Default value
if test x$ENABLE_DEBUG = xyes; then
  AC_DEFINE(GST_DEBUG_ENABLED, 1, [Define if DEBUG statements should be compiled in])
fi

AC_ARG_ENABLE(INFO,
[  --disable-INFO               disables compilation of informational messages],
[case "${enableval}" in
  yes) ENABLE_INFO=yes ;;
  no)  ENABLE_INFO=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-INFO) ;;
esac], 
[ENABLE_INFO=yes]) dnl Default value
if test x$ENABLE_INFO = xyes; then
  AC_DEFINE(GST_INFO_ENABLED, 1, [Define if INFO statements should be compiled in])
fi

AC_ARG_ENABLE(debug-color,
[  --disable-debug-color        disables color output of DEBUG and INFO output],
[case "${enableval}" in
  yes) ENABLE_DEBUG_COLOR=yes ;;
  no)  ENABLE_DEBUG_COLOR=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-debug-color) ;;
esac], 
[ENABLE_DEBUG_COLOR=yes]) dnl Default value
if test "x$ENABLE_DEBUG_COLOR" = xyes; then
  AC_DEFINE(GST_DEBUG_COLOR, 1, [Define if debugging messages should be colorized])
fi

AC_ARG_ENABLE(profiling,
[  --enable-profiling           adds -pg to compiler commandline, for profiling],
[case "${enableval}" in
  yes) USE_PROFILING=yes ;;
  no)  UES_PROFILING=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-profiling) ;;
esac], 
[USE_PROFILING=no]) dnl Default value

dnl default to building registry in the source tree if we are enabling plugin build dir
if test "x$PLUGINS_USE_BUILDDIR"="xyes"; then
  GST_CONFIG_DIR=`pwd`
else
  GST_CONFIG_DIR=/etc/gstreamer
fi
AC_ARG_WITH(configdir,
[  --with-configdir             specify path to use for configdir],
[case "${withval}" in
  yes) AC_MSG_ERROR(bad value ${withval} for --with-configdir) ;;
  no) AC_MSG_ERROR(bad value ${withval} for --with-configdir) ;;
  *) GST_CONFIG_DIR="${withval}" ;;
esac], 
[:]) dnl Default value

dnl Default value
GST_WIN32_LIBDIR="/usr/lib/win32"
AC_ARG_WITH(win32_libdir,
[  --with-win32-libdir          specify location for win32 DLLs],
[case "${withval}" in
  yes) AC_MSG_ERROR(bad value ${withval} for --with-win32-libdir) ;;
  no) AC_MSG_ERROR(bad value ${withval} for --with-win32-libdir) ;;
  *) GST_WIN32_LIBDIR="${withval}" ;;
esac],
[:]) dnl Default value

AC_ARG_ENABLE(docs-build,
[  --enable-docs-build          enable building of documentation],
[case "${enableval}" in
  yes) if $HAVE_GTK_DOC; then BUILD_DOCS=yes; else AC_MSG_ERROR([you don't have gtk-doc, so don't use --docs-build]); fi; ;;
  no)  BUILD_DOCS=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-docs-build) ;;
esac], 
[BUILD_DOCS=no]) dnl Default value

AC_ARG_ENABLE(plugin-docs,
[  --enable-plugin-docs         enable the building of plugin documentation
                               (this is currently broken, so off by default)],
[case "${enableval}" in
  yes) BUILD_PLUGIN_DOCS=yes ;;
  no)  BUILD_PLUGIN_DOCS=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --enable-plugin-docs) ;;
esac], 
[BUILD_PLUGIN_DOCS=no]) dnl Default value

AC_ARG_ENABLE(tests,
[  --disable-tests              disable building test apps],
[case "${enableval}" in
  yes) BUILD_TESTS=yes ;;
  no)  BUILD_TESTS=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-tests) ;;
esac], 
[BUILD_TESTS=yes]) dnl Default value

AC_ARG_ENABLE(examples,
[  --disable-examples           disable building examples],
[case "${enableval}" in
  yes) BUILD_EXAMPLES=yes ;;
  no)  BUILD_EXAMPLES=no ;;
  *) AC_MSG_ERROR(bad value ${enableval} for --disable-examples) ;;
esac], 
[BUILD_EXAMPLES=yes]) dnl Default value

dnl Next, check for the optional components:
dnl ========================================


AC_DEFUN(GST_SUBSYSTEM_DISABLE,
[dnl
dnl Add a subsystem --disable flag and all the necessary symbols and substitions
dnl
AC_ARG_ENABLE(translit([$1], A-Z, a-z), 
[  ]builtin(format, --disable-%-17s  disable %s, translit([$1], A-Z, a-z), $2),
[ case "${enableval}" in
    yes) GST_DISABLE_[$1]=no ;;
    no) GST_DISABLE_[$1]=yes ;;
    *) AC_MSG_ERROR(bad value ${enableval} for --enable-translit([$1], A-Z, a-z)) ;;
  esac],
[GST_DISABLE_[$1]=no]) dnl Default value
if test x$GST_DISABLE_[$1] = xyes; then
  AC_DEFINE(GST_DISABLE_[$1], 1, [Disable $2])
  GST_DISABLE_[$1]_DEFINE=-DGST_DISABLE_[$1]
fi
AM_CONDITIONAL(GST_DISABLE_[$1], test x$GST_DISABLE_[$1] = xyes)
AC_SUBST(GST_DISABLE_[$1]_DEFINE)
GST_SUBSYSTEM_DISABLE_DEFINES="$GST_SUBSYTEM_DISABLE_DEFINES $GST_DISABLE_[$1]_DEFINE"
])

translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_LOADSAVE, true)
GST_SUBSYSTEM_DISABLE(LOADSAVE,[pipeline XML load/save])
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_TYPEFIND, true)
GST_SUBSYSTEM_DISABLE(TYPEFIND,[typefind plugin],)
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_AUTOPLUG, true)
GST_SUBSYSTEM_DISABLE(AUTOPLUG,[autoplugger subsystem])
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_PARSE, true)
GST_SUBSYSTEM_DISABLE(PARSE,[command-line parser])
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_TRACE, true)
GST_SUBSYSTEM_DISABLE(TRACE,[tracing subsystem])
translit(dnm, m, l) AM_CONDITIONAL(GST_DISABLE_REGISTRY, true)
GST_SUBSYSTEM_DISABLE(REGISTRY,[plugin registry])

GST_CFLAGS="$GST_CFLAGS $GST_SUBSYSTEM_DISABLE_DEFINES"

dnl for pkg-config
GST_PKG_CFLAGS=$GST_CFLAGS
AC_SUBST(GST_PKG_CFLAGS)

dnl ################################################
dnl # Set defines according to variables set above #
dnl ################################################


dnl These should be "USE_*" instead of "HAVE_*", but some packages expect
dnl HAVE_ and it is likely to be easier to stick with the old name
if test "x$USE_LIBGHTTP" = xyes; then
  AC_DEFINE(HAVE_LIBGHTTP, 1, [Define if ghttp library is available])
fi

if test "x$USE_LIBMMX" = xyes; then
  AC_DEFINE(HAVE_LIBMMX, 1, [Define if libmmx is available])
fi

if test "x$USE_ATOMIC_H" = xyes; then
  AC_DEFINE(HAVE_ATOMIC_H, 1, [Define if atomic.h header file is available])
fi

if test "x$PLUGINS_USE_BUILDDIR" = xyes; then
  AC_DEFINE(PLUGINS_USE_BUILDDIR, 1, [Define if plugins should be loaded from the build tree - only developers should use this])
fi

dnl if test "x$USE_DEBUG" = xyes; then
dnl   CFLAGS="$CFLAGS -g"
dnl fi

if test "x$USE_PROFILING" = xyes; then
dnl  CFLAGS="$CFLAGS -pg -fprofile-arcs"
  FOMIT_FRAME_POINTER=""
else
  FOMIT_FRAME_POINTER="-fomit-frame-pointer"
fi

dnl
dnl AC_SUBST(FOMIT_FRAME_POINTER)
dnl

if test "x$HAVE_LIBXV" = xyes; then
  AC_DEFINE(HAVE_LIBXV)
fi

dnl #############################
dnl # Set automake conditionals #
dnl #############################

dnl These should be "USE_*" instead of "HAVE_*", but some packages expect
dnl HAVE_ and it is likely to be easier to stick with the old name
AM_CONDITIONAL(HAVE_CPU_I386,       test "x$HAVE_CPU_I386" = "xyes")
AM_CONDITIONAL(HAVE_CPU_PPC,        test "x$HAVE_CPU_PPC" = "xyes")
AM_CONDITIONAL(HAVE_CPU_ALPHA,      test "x$HAVE_CPU_ALPHA" = "xyes")
AM_CONDITIONAL(HAVE_CPU_ARM,        test "x$HAVE_CPU_ARM" = "xyes")
AM_CONDITIONAL(HAVE_CPU_SPARC,      test "x$HAVE_CPU_SPARC" = "xyes")
AM_CONDITIONAL(HAVE_LIBMMX,         test "x$USE_LIBMMX" = "xyes")

AM_CONDITIONAL(HAVE_ATOMIC_H,       test "x$USE_ATOMIC_H" = "xyes")

AM_CONDITIONAL(EXPERIMENTAL,        test "$EXPERIMENTAL" = "$xyes")
AM_CONDITIONAL(BROKEN,              test "$BROKEN" = "$xyes")

AM_CONDITIONAL(HAVE_NASM,           test "x$HAVE_NASM" = "xyes")
AM_CONDITIONAL(HAVE_LIBGLADE_GNOME, test "x$HAVE_LIBGLADE_GNOME" = "xyes")
AM_CONDITIONAL(HAVE_GNOME,          test "x$HAVE_GNOME" = "xyes")
AM_CONDITIONAL(HAVE_LIBXV,          test "x$HAVE_LIBXV" = "xyes")
AM_CONDITIONAL(HAVE_GTK,            test "x$HAVE_GTK" = "xyes")
AM_CONDITIONAL(HAVE_GTK_DOC,        $HAVE_GTK_DOC)
AM_CONDITIONAL(BUILD_DOCS,          test "x$BUILD_DOCS" = "xyes")
AM_CONDITIONAL(BUILD_TESTS,         test "x$BUILD_TESTS" = "xyes")
AM_CONDITIONAL(BUILD_EXAMPLES,      test "x$BUILD_EXAMPLES" = "xyes")
AM_CONDITIONAL(BUILD_PLUGIN_DOCS,   test "x$BUILD_PLUGIN_DOCS" = "xyes")
AM_CONDITIONAL(HAVE_PDFXMLTEX,      $HAVE_PDFXMLTEX)
AM_CONDITIONAL(HAVE_PDFTOPS,        $HAVE_PDFTOPS)
AM_CONDITIONAL(HAVE_XSLTPROC,       $HAVE_XSLTPROC)
AM_CONDITIONAL(HAVE_FIG2DEV_PNG,    $HAVE_FIG2DEV_PNG)
AM_CONDITIONAL(HAVE_FIG2DEV_PDF,    $HAVE_FIG2DEV_PDF)
AM_CONDITIONAL(PLUGINS_USE_BUILDDIR,  test "x$PLUGINS_USE_BUILDDIR" = "xyes")


dnl ############################
dnl # Set up some more defines #
dnl ############################

dnl Set location of configuration dir.
AC_DEFINE_UNQUOTED(GST_CONFIG_DIR,"$GST_CONFIG_DIR")
AC_SUBST(GST_CONFIG_DIR)

dnl Set location of windows dll dir.
AC_DEFINE_UNQUOTED(GST_WIN32_LIBDIR,"$GST_WIN32_LIBDIR")
AC_SUBST(GST_WIN32_LIBDIR)

dnl Set location of plugin directory
if test "x${prefix}" = "xNONE"; then
  PLUGINS_DIR=${ac_default_prefix}/lib/gst
else
  PLUGINS_DIR=${prefix}/lib/gst
fi
AC_DEFINE_UNQUOTED(PLUGINS_DIR,"$PLUGINS_DIR")
AC_SUBST(PLUGINS_DIR)

dnl Set location of uninstalled plugin directory
PLUGINS_BUILDDIR=`pwd`
AC_DEFINE_UNQUOTED(PLUGINS_BUILDDIR,"$PLUGINS_BUILDDIR")
AC_SUBST(PLUGINS_BUILDDIR)

dnl Private vars for libgst only
LIBGST_LIBS="$GST_LIBS $XML_LIBS $GLIB_LIBS"
LIBGST_CFLAGS="$GST_CFLAGS $XML_CFLAGS $GLIB_CFLAGS"
AC_SUBST(LIBGST_LIBS)
AC_SUBST(LIBGST_CFLAGS)

dnl Vars for everyone else
GST_LIBS="\$(top_builddir)/gst/libgst.la $LIBGST_LIBS"
GST_CFLAGS="-I\$(top_srcdir) -I\$(top_srcdir)/libs -I\$(top_srcdir)/include $LIBGST_CFLAGS"
AC_SUBST(GST_LIBS)
AC_SUBST(GST_CFLAGS)

dnl ##################################################
dnl # Prepare informative messages to display at end #
dnl ##################################################

infomessages=

if test "x$PLUGINS_USE_BUILDDIR" = xyes; then
  infomessages="$infomessages
*** Warning: You have configured using the --enable-plugin-builddir option.

This option is for development purposes only: binaries built with
it should be used with code in the build tree only.  To build an
installable version, use ./configure without the --enable-plugin-builddir
option.  Note that the autogen.sh script supplies the plugin builddir
option automatically - it cannot be used to configure installable builds.

"
fi

dnl #########################
dnl # Make the output files #
dnl #########################

dnl until ffmpeg is handled by configure plugins/ffmpeg/Makefile
dnl components/bonobo-gstmediaplay/Makefile
dnl someone should fix this test/misc/Makefile
dnl wtay fix this: testsuite/threads/Makefile
dnl testsuite/refcounting/Makefile
AC_OUTPUT(
Makefile
include/Makefile
gst/Makefile
gst/gstversion.h
gst/types/Makefile
gst/elements/Makefile
gst/autoplug/Makefile
gst/schedulers/Makefile
libs/Makefile
libs/gst/Makefile
libs/gst/bytestream/Makefile
libs/gst/getbits/Makefile
libs/gst/putbits/Makefile
libs/gst/control/Makefile
gstplay/Makefile
test/Makefile
test/xml/Makefile
test/bindings/Makefile
test/memchunk/Makefile
test/events/Makefile
tests/Makefile
tests/sched/Makefile
tests/eos/Makefile
tests/muxing/Makefile
testsuite/Makefile
testsuite/bytestream/Makefile
testsuite/capsnego/Makefile
testsuite/plugin/Makefile
testsuite/cleanup/Makefile
tests/nego/Makefile
examples/Makefile
examples/autoplug/Makefile
examples/helloworld/Makefile
examples/helloworld2/Makefile
examples/launch/Makefile
examples/queue/Makefile
examples/queue2/Makefile
examples/queue3/Makefile
examples/queue4/Makefile
examples/thread/Makefile
examples/mixer/Makefile
examples/cutter/Makefile
examples/launch/Makefile
examples/xml/Makefile
examples/plugins/Makefile
examples/typefind/Makefile
examples/mixer/Makefile
editor/Makefile
editor/pixmaps/Makefile
tools/Makefile
docs/Makefile
docs/gst/Makefile
docs/gst/gstreamer.types
docs/libs/Makefile
docs/plugins/Makefile
docs/plugins/gstreamer-plugins.types
docs/manual/Makefile
docs/fwg/Makefile
stamp.h
gstreamer.pc
gstreamer-uninstalled.pc
gstreamer.spec,
echo "$infomessages", infomessages="$infomessages"
)
