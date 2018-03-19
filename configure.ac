AC_PREREQ(2.69)

dnl initialize autoconf
dnl when going to/from release please set the nano (fourth number) right !
dnl releases only do Wall, cvs and prerelease does Werror too
AC_INIT(GStreamer libav, 1.14.0,
    http://bugzilla.gnome.org/enter_bug.cgi?product=GStreamer,
    gst-libav)

AG_GST_INIT

dnl initialize automake
AM_INIT_AUTOMAKE([-Wno-portability 1.14 no-dist-gzip dist-xz tar-ustar subdir-objects])

dnl define PACKAGE_VERSION_* variables
AS_VERSION

dnl check if this is a release version
AS_NANO(GST_GIT="no", GST_GIT="yes")

dnl can autoconf find the source ?
AC_CONFIG_SRCDIR([ext/libav/gstav.c])

dnl define the output header for config
AC_CONFIG_HEADERS([config.h])

dnl AM_MAINTAINER_MODE only provides the option to configure to enable it
AM_MAINTAINER_MODE([enable])

dnl sets host_* variables
AC_CANONICAL_HOST

dnl use pretty build output by default
AM_SILENT_RULES([yes])

dnl our libraries and install dirs use major.minor as a version
GST_API_VERSION=$PACKAGE_VERSION_MAJOR.$PACKAGE_VERSION_MINOR
dnl we override it here if we need to for the release candidate of new series
GST_API_VERSION=1.0
AC_SUBST(GST_API_VERSION)

dnl *** Check for external $AS vs detected by AS_LIBTOOL
user_AS="$AS"
AG_GST_LIBTOOL_PREPARE
AS_LIBTOOL(GST, 1400, 0, 1400)

dnl *** required versions of GStreamer stuff ***
GST_REQ=1.14.0
GST_PBREQ=1.14.0

ORC_REQ=0.4.16
ORC_CHECK([$ORC_REQ])

dnl *** autotools stuff ****

dnl allow for different autotools
AS_AUTOTOOLS_ALTERNATE

dnl Add parameters for aclocal
AC_SUBST(ACLOCAL_AMFLAGS, "-I m4 -I common/m4")

dnl *** check for arguments to configure ***

AG_GST_ARG_DISABLE_FATAL_WARNINGS
AG_GST_ARG_ENABLE_EXTRA_CHECKS

AG_GST_ARG_WITH_PKG_CONFIG_PATH
AG_GST_ARG_WITH_PACKAGE_NAME
AG_GST_ARG_WITH_PACKAGE_ORIGIN
AG_GST_ARG_VALGRIND
AG_GST_ARG_GCOV

dnl *** checks for platform ***

dnl * hardware/architecture *

dnl common/m4/gst-arch.m4
dnl check CPU type
AG_GST_ARCH

dnl *** software ***

dnl check for large file support
dnl affected plugins must include config.h
AC_SYS_LARGEFILE

dnl *** checks for programs ***

dnl find a compiler
AC_PROG_CC
AC_PROG_CC_STDC

dnl check if the compiler supports '-c' and '-o' options
AM_PROG_CC_C_O

AC_PATH_PROG(VALGRIND_PATH, valgrind, no)
AM_CONDITIONAL(HAVE_VALGRIND, test ! "x$VALGRIND_PATH" = "xno")

dnl check for documentation tools
GTK_DOC_CHECK([1.12])
AG_GST_PLUGIN_DOCS([1.12])

dnl *** checks for libraries ***

dnl check for libm, for sin()
LT_LIB_M
AC_SUBST(LIBM)

dnl check for pthreads
AX_PTHREAD

dnl *** checks for header files ***

dnl check if we have ANSI C header files
AC_HEADER_STDC

dnl *** checks for types/defines ***

dnl *** checks for structures ***

dnl *** checks for compiler characteristics ***

dnl *** checks for library functions ***

dnl *** checks for dependancy libraries ***

dnl GLib is required
GLIB_REQ=2.40.0
AC_SUBST([GLIB_REQ])
AG_GST_GLIB_CHECK([$GLIB_REQ])

dnl checks for gstreamer
dnl uninstalled is selected preferentially -- see pkg-config(1)
AG_GST_CHECK_GST($GST_API_VERSION, [$GST_REQ])
AG_GST_CHECK_GST_BASE($GST_API_VERSION, [$GST_REQ])
AG_GST_CHECK_GST_PLUGINS_BASE($GST_API_VERSION, [$GST_PBREQ])
AG_GST_CHECK_GST_CHECK($GST_API_VERSION, [$GST_REQ], no)
AM_CONDITIONAL(HAVE_GST_CHECK, test "x$HAVE_GST_CHECK" = "xyes")

AC_MSG_NOTICE(Using GStreamer Core Plugins in $GST_PLUGINS_DIR)
AC_MSG_NOTICE(Using GStreamer Base Plugins in $GSTPB_PLUGINS_DIR)

dnl Check for -Bsymbolic-functions linker flag used to avoid
dnl intra-library PLT jumps, if available.
AC_ARG_ENABLE(Bsymbolic,
              [AS_HELP_STRING([--disable-Bsymbolic],[avoid linking with -Bsymbolic])],,
              [SAVED_LDFLAGS="${LDFLAGS}" SAVED_LIBS="${LIBS}"
               AC_MSG_CHECKING([for -Bsymbolic linker flag])
               LDFLAGS=-Wl,-Bsymbolic
               LIBS=
               AC_TRY_LINK([], [return 0],
                           AC_MSG_RESULT(yes)
                           enable_Bsymbolic=yes,
                           AC_MSG_RESULT(no)
                           enable_Bsymbolic=no)
               LDFLAGS="${SAVED_LDFLAGS}" LIBS="${SAVED_LIBS}"])

dnl *** set variables based on configure arguments ***

dnl set location of plugin directory
AG_GST_SET_PLUGINDIR

dnl Detect if we are building static plugins, in which case we need to
dnl generates libav .la files and install the static libraries.
AM_CONDITIONAL(GST_PLUGIN_BUILD_STATIC, test "x$enable_static" = "xyes")

dnl define an ERROR_CFLAGS Makefile variable
AG_GST_SET_ERROR_CFLAGS($FATAL_WARNINGS, [
    -Wmissing-declarations -Wmissing-prototypes -Wredundant-decls -Wundef
    -Wwrite-strings -Wformat-nonliteral -Wformat-security -Wold-style-definition
    -Winit-self -Wmissing-include-dirs -Waddress
    -Waggregate-return -Wno-multichar -Wnested-externs ])

dnl define correct level for debugging messages
AG_GST_SET_LEVEL_DEFAULT($GST_GIT)

dnl disable strict aliasing
AS_COMPILER_FLAG([-fno-strict-aliasing], [EXTRA_CFLAGS="-fno-strict-aliasing"])
AC_SUBST(EXTRA_CFLAGS)

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

if test "x$GST_GIT" = "xyes"; then
  DEPRECATED_CFLAGS="-DGST_DISABLE_DEPRECATED"
else
  DEPRECATED_CFLAGS=""
fi
AC_SUBST(DEPRECATED_CFLAGS)

dnl every flag in GST_OPTION_CFLAGS can be overridden at make time
GST_OPTION_CFLAGS="\$(WARNING_CFLAGS) \$(ERROR_CFLAGS) \$(DEBUG_CFLAGS) \$(PROFILE_CFLAGS) \$(GCOV_CFLAGS) \$(OPT_CFLAGS) \$(DEPRECATED_CFLAGS)"

AC_SUBST(GST_OPTION_CFLAGS)

dnl FIXME: do we want to rename to GST_ALL_* ?
dnl prefer internal headers to already installed ones
dnl also add builddir include for enumtypes and marshal
dnl add GST_OPTION_CFLAGS, but overridable
GST_CFLAGS="$GST_CFLAGS"
GST_CFLAGS="-I\$(top_srcdir)/gst-libs -I\$(top_builddir)/gst-libs $GST_CFLAGS $EXTRA_CFLAGS $GLIB_EXTRA_CFLAGS \$(GST_OPTION_CFLAGS)"
AC_SUBST(GST_CFLAGS)
AC_SUBST(GST_LIBS)

GST_ALL_LDFLAGS="-no-undefined"
if test "x${enable_Bsymbolic}" = "xyes"; then
  GST_ALL_LDFLAGS="$GST_ALL_LDFLAGS -Wl,-Bsymbolic"
fi
AC_SUBST(GST_ALL_LDFLAGS)

dnl this really should only contain flags, not libs - they get added before
dnl whatevertarget_LIBS and -L flags here affect the rest of the linking
GST_PLUGIN_LDFLAGS="-module -avoid-version -export-symbols-regex '^[_]*gst_plugin_.*' $GST_ALL_LDFLAGS"
AC_SUBST(GST_PLUGIN_LDFLAGS)

dnl Add MacOSX specific flags
AC_CANONICAL_HOST
case $host_os in
     darwin*)
	DARWIN_LDFLAGS="-Wl,-read_only_relocs,suppress"
	;;
     *)
        DARWIN_LDFLAGS=""
	;;
esac
AC_SUBST(DARWIN_LDFLAGS)

AC_ARG_ENABLE(gpl,
              [AC_HELP_STRING([--enable-gpl], [build a GPL licensed gst-libav])])
AM_CONDITIONAL(GST_LIBAV_ENABLE_GPL, test "x$enable_gpl" = "xyes")
if test "x$enable_gpl" = "xyes"; then
  AC_DEFINE([GST_LIBAV_ENABLE_GPL], [], [Defined if building a GPL-licensed version of gst-libav])
fi

dnl *** configure external libs ***

HAVE_LIBAV_UNINSTALLED=1

AC_ARG_WITH(system-libav,
            [AC_HELP_STRING([--with-system-libav], [use system Libav libraries])])

HAVE_LZMA="no"
HAVE_BZ2="no"
if test "x$with_system_libav" = "xyes"; then
  PKG_CHECK_MODULES(LIBAV, libavfilter libavformat libavcodec >= 57 libavutil)
  PKG_CHECK_MODULES(SWSCALE, libswscale libavutil)
  saved_CPPFLAGS="$CPPFLAGS"
  CPPFLAGS="$CPPFLAGS $LIBAV_CFLAGS"
  AC_CHECK_HEADERS([avi.h])
  AC_DEFINE([LIBAV_SOURCE], ["system install"], [Describes where the Libav libraries come from.])
  HAVE_LIBAV_UNINSTALLED=0
  AC_MSG_NOTICE([Using system-installed libav code])
  AC_MSG_CHECKING([whether libav is provided by FFmpeg])
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[
    #include <libavcodec/avcodec.h>
    ]],[[
        #if LIBAVCODEC_VERSION_MICRO >= 100
        /* FFmpeg uses 100+ as its micro version */
        #else
        #error libav provider should be FFmpeg
        #endif
    ]])], [is_ffmpeg=yes], [is_ffmpeg=no])
  AC_MSG_RESULT([$is_ffmpeg])
  CPPFLAGS="$saved_CPPFLAGS"
  if test "$is_ffmpeg" = no; then
    AC_MSG_ERROR([Uncompatible libavcodec found])
  fi
else
  AC_MSG_NOTICE([Using local Libav snapshot])

  dnl *** Check for bz2
  save_LIBS=$LIBS
  LIBS="$LIBS -lbz2"
  AC_MSG_CHECKING([for BZ2_bzlibVersion in -lbz2])
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <bzlib.h>]], [[const char *version = BZ2_bzlibVersion ();]])],[HAVE_BZ2=yes],[HAVE_BZ2=no])
  AC_MSG_RESULT($HAVE_BZ2)
  LIBS=$save_LIBS
  if test "x$HAVE_BZ2" = "xno"; then
    AC_WARN([libbz2 not found, matroska demuxer will not be able to read bz2 tracks])
    BZ2_LIBS=
  else
    BZ2_LIBS="-lbz2"
  fi

  dnl *** Check for lzma
  save_LIBS=$LIBS
  LIBS="$LIBS -llzma"
  AC_MSG_CHECKING([for lzma_version_string in -llzma])
  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <lzma.h>]], [[const char *version = lzma_version_string ();]])],[HAVE_LZMA=yes],[HAVE_LZMA=no])
  AC_MSG_RESULT($HAVE_LZMA)
  LIBS=$save_LIBS
  if test "x$HAVE_LZMA" = "xno"; then
    AC_WARN([lzma not found, tiff reader will not be able to read lzma files])
    LZMA_LIBS=
  else
    LZMA_LIBS="-llzma"
  fi

  dnl libgstlibav.la: include dirs
  LIBAV_CPPFLAGS="-I \$(top_srcdir)/gst-libs/ext/libav \
                -I \$(top_builddir)/gst-libs/ext/libav"

  LIBAV_CFLAGS="-Wno-deprecated-declarations"

  LIBAV_DEPS="\$(top_builddir)/gst-libs/ext/libav/libavfilter/libavfilter.a \
              \$(top_builddir)/gst-libs/ext/libav/libavformat/libavformat.a \
              \$(top_builddir)/gst-libs/ext/libav/libavcodec/libavcodec.a \
              \$(top_builddir)/gst-libs/ext/libav/libswresample/libswresample.a \
              \$(top_builddir)/gst-libs/ext/libav/libavutil/libavutil.a"

  LIBAV_LIBS="$ac_pwd/gst-libs/ext/libavfilter.la \
              $ac_pwd/gst-libs/ext/libavformat.la \
              $ac_pwd/gst-libs/ext/libavcodec.la \
              $ac_pwd/gst-libs/ext/libswresample.la \
              $ac_pwd/gst-libs/ext/libavutil.la"

  dnl
  SWSCALE_CFLAGS="-I \$(top_srcdir)/gst-libs/ext/libav \
                  -I \$(top_builddir)/gst-libs/ext/libav \
                  -Wno-deprecated-declarations"

  SWSCALE_DEPS="\$(top_builddir)/gst-libs/ext/libav/libswscale/libswscale.a \
                \$(top_builddir)/gst-libs/ext/libav/libavutil/libavutil.a"
  if test "x$enable_static_plugins" = xyes; then
      dnl with static linking we can't use the .a archive directly as they would
      dnl be included in the final libgstswscale.a as a file and won't be usable.
      dnl libav*.a must be copied to the final destination too
      SWSCALE_LIBS="-lswscale -lavutil"
  else
      dnl libgstswscale.la: libs to statically link to
      SWSCALE_LIBS="$SWSCALE_DEPS"
  fi

  LIBAV_SUBDIRS=gst-libs
  AC_DEFINE(HAVE_AVI_H)
  AC_DEFINE([LIBAV_SOURCE], ["local snapshot"], [Describes where the Libav libraries come from.])

  AC_ARG_WITH(libav-extra-configure,
      AC_HELP_STRING([--with-libav-extra-configure="xxx"],
      [extra configure options for internal libav ./configure script]),,
      with_libav=no)

  # basic arguments
  emblibav_configure_args="--prefix=$prefix"

  # Enable pic and static so that we get .a files, but with PIC code.
  emblibav_configure_args="$emblibav_configure_args \
        --enable-static --enable-pic \
        --disable-avdevice --disable-postproc \
        --disable-programs --disable-ffserver --disable-ffplay --disable-ffprobe --disable-ffmpeg \
        --disable-encoder=flac --disable-protocols --disable-devices \
        --disable-network --disable-hwaccels --disable-dxva2 --disable-vdpau \
        --disable-filters --enable-filter=yadif --disable-doc --disable-vda --disable-d3d11va --disable-dxva2 \
        --disable-audiotoolbox --disable-videotoolbox --disable-vaapi --disable-crystalhd \
        --disable-mediacodec --disable-nvenc --disable-mmal --disable-omx \
        --disable-omx-rpi --disable-cuda --disable-cuvid --disable-libmfx \
        --disable-libnpp --disable-iconv --disable-jni --disable-v4l2_m2m --enable-optimizations"

  if test "x$enable_gpl" == "xyes"; then
    emblibav_configure_args="$emblibav_configure_args --enable-gpl"
  fi

  # if we are cross-compiling, tell libav so
  case $host in
      *android*)
        target_os=linux
      ;;
      *darwin*)
        target_os=darwin
      ;;
      *)
        target_os=`echo $host_os | sed 's/-gnu.*//'`
      ;;
  esac

  if test "x$cross_compiling" = xyes; then
    emblibav_configure_args="$emblibav_configure_args --enable-cross-compile \
        --target-os=$target_os --arch=$host_cpu --cross-prefix=$host_alias-"
  fi

  case $host_os in
    mingw32*)
      WIN32_LIBS="-lws2_32"
      ;;
    *)
      WIN32_LIBS=
      ;;
  esac

  if test x"$AR" != x; then
    emblibav_configure_args="$emblibav_configure_args --ar=\\\"\\\$AR\\\""
  fi

  if test x"$orig_AS" != x; then
    emblibav_configure_args="$emblibav_configure_args --as=\\\"\\\$orig_AS\\\""
  elif test x"$CC" != x; then
    dnl No external $AS - try $CC for the libav assembler
    emblibav_configure_args="$emblibav_configure_args --as=\\\"\\\$CC\\\""
  fi

  if test x"$CC" != x; then
    emblibav_configure_args="$emblibav_configure_args --cc=\\\"\\\$CC\\\""
  fi

  if test x"$LD" != x; then
    emblibav_configure_args="$emblibav_configure_args --ld=\\\"\\\$CC\\\""
  fi

  if test x"$NM" != x; then
    emblibav_configure_args="$emblibav_configure_args --nm=\\\"\\\$NM\\\""
  fi

  dnl checks for extra enable/disable flags
  LIBAV_OPTS=`cd $srcdir/gst-libs/ext/libav && ./configure --help`
  # Let's check if we can disable the building of the libav binary
  can_disable=`echo "$LIBAV_OPTS" | grep 'disable-ffmpeg'`
  if test "$can_disable" != ""; then
    emblibav_configure_args="$emblibav_configure_args --disable-ffmpeg"
  fi
  dnl check if libswscale needs enabling explicitly
  can_enable=`echo "$LIBAV_OPTS" | grep 'enable-swscale'`
  if test "$can_enable" != ""; then
    emblibav_configure_args="$emblibav_configure_args --enable-swscale"
  fi

  # append extra configure options to emblibav_configure_args if needed
  if test "x$with_libav_extra_configure" != "xno"; then
    emblibav_configure_args="$emblibav_configure_args $with_libav_extra_configure"
  fi

  AC_SUBST(LIBAV_CO_DIR)
  AC_SUBST(LIBAV_SVN)
  AC_SUBST(LIBAV_REVISION)
  AC_SUBST(LIBAV_EXTERNALS_REVISION)
  AC_CONFIG_COMMANDS([configure-embedded-libav],
    [echo "Configuring included Libav instance with args $emblibav_configure_args"
     origdir=`pwd`
     dnl Don't put path on the configure call when not needed, as FFmpeg's configure relies on it
     dnl to detect out-of-tree builds
     if test -z "$srcdir" -o "$srcdir" = .; then
       confcmd=./configure
       gasppdir=../gas-preprocessor
     else
       confcmd="$ac_abs_top_srcdir"/gst-libs/ext/libav/configure
       gasppdir="$ac_abs_top_srcdir"/gst-libs/ext/gas-preprocessor
     fi

     AS_MKDIR_P(["$ac_top_build_prefix"gst-libs/ext/libav])
     oldPATH="$PATH"
     PATH="$gasppdir:$PATH"
     cd "$ac_top_build_prefix"gst-libs/ext/libav &&
         eval "$confcmd $emblibav_configure_args" ||
         AC_MSG_ERROR([Failed to configure embedded Libav tree])
     PATH="$oldPATH"
     cd "$origdir"
    ],
    [emblibav_configure_args="$emblibav_configure_args"])
  AC_MSG_NOTICE([Using included Libav code])
fi

AM_CONDITIONAL(HAVE_BZ2, test "x$HAVE_BZ2" = "xyes")
AM_CONDITIONAL(HAVE_LZMA, test "x$HAVE_LZMA" = "xyes")

AC_SUBST(LIBAV_CPPFLAGS)
AC_SUBST(LIBAV_CFLAGS)
AC_SUBST(LIBAV_DEPS)
AC_SUBST(LIBAV_LIBS)
AC_SUBST(LIBAV_SUBDIRS)
AC_SUBST(SWSCALE_CFLAGS)
AC_SUBST(SWSCALE_DEPS)
AC_SUBST(SWSCALE_LIBS)
AC_SUBST(WIN32_LIBS)
AC_SUBST(BZ2_LIBS)
AC_SUBST(LZMA_LIBS)

if test x$HAVE_LIBAV_UNINSTALLED = x1; then
  AC_DEFINE(HAVE_LIBAV_UNINSTALLED, [], [Defined if building against uninstalled Libav source])
fi
AM_CONDITIONAL(HAVE_LIBAV_UNINSTALLED, test x$HAVE_LIBAV_UNINSTALLED = x1)

AC_CONFIG_FILES(
Makefile
common/Makefile
common/m4/Makefile
ext/Makefile
ext/libav/Makefile
ext/libswscale/Makefile
gst-libs/Makefile
gst-libs/ext/Makefile
docs/Makefile
docs/plugins/Makefile
docs/version.entities
tests/Makefile
tests/check/Makefile
tests/files/Makefile
pkgconfig/Makefile
pkgconfig/gstreamer-plugins-libav-uninstalled.pc
)
AC_OUTPUT

ORC_OUTPUT
