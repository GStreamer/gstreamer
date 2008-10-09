dnl configure-time options shared among gstreamer modules

dnl AG_GST_ARG_DEBUG
dnl AG_GST_ARG_PROFILING
dnl AG_GST_ARG_VALGRIND
dnl AG_GST_ARG_GCOV

dnl AG_GST_ARG_EXAMPLES

dnl AG_GST_ARG_WITH_PKG_CONFIG_PATH
dnl AG_GST_ARG_WITH_PACKAGE_NAME
dnl AG_GST_ARG_WITH_PACKAGE_ORIGIN

dnl AG_GST_ARG_WITH_PLUGINS

dnl AG_GST_ARG_ENABLE_EXTERNAL
dnl AG_GST_ARG_ENABLE_EXPERIMENTAL
dnl AG_GST_ARG_ENABLE_BROKEN

AC_DEFUN([AG_GST_ARG_DEBUG],
[
  dnl debugging stuff
  AC_ARG_ENABLE(debug,
    AC_HELP_STRING([--disable-debug],[disable addition of -g debugging info]),
    [
      case "${enableval}" in
        yes) USE_DEBUG=yes ;;
        no)  USE_DEBUG=no ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-debug) ;;
      esac
    ],
    [USE_DEBUG=yes]) dnl Default value
])

AC_DEFUN([AG_GST_ARG_PROFILING],
[
  AC_ARG_ENABLE(profiling,
    AC_HELP_STRING([--enable-profiling],
      [adds -pg to compiler commandline, for profiling]),
    [
      case "${enableval}" in
        yes) USE_PROFILING=yes ;;
        no)  USE_PROFILING=no ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-profiling) ;;
      esac
    ], 
    [USE_PROFILING=no]) dnl Default value
])

AC_DEFUN([AG_GST_ARG_VALGRIND],
[
  dnl valgrind inclusion
  AC_ARG_ENABLE(valgrind,
    AC_HELP_STRING([--disable-valgrind],[disable run-time valgrind detection]),
    [
      case "${enableval}" in
        yes) USE_VALGRIND="$USE_DEBUG" ;;
        no)  USE_VALGRIND=no ;;
        *)   AC_MSG_ERROR(bad value ${enableval} for --enable-valgrind) ;;
      esac
    ],
    [USE_VALGRIND="$USE_DEBUG"]) dnl Default value
  VALGRIND_REQ="2.1"
  if test "x$USE_VALGRIND" = xyes; then
    PKG_CHECK_MODULES(VALGRIND, valgrind > $VALGRIND_REQ,
      USE_VALGRIND="yes",
      [
        USE_VALGRIND="no"
        AC_MSG_RESULT([no])
      ])
  fi
  if test "x$USE_VALGRIND" = xyes; then
    AC_DEFINE(HAVE_VALGRIND, 1, [Define if valgrind should be used])
    AC_MSG_NOTICE(Using extra code paths for valgrind)
  fi
])

AC_DEFUN([AG_GST_ARG_GCOV],
[
  AC_ARG_ENABLE(gcov,
    AC_HELP_STRING([--enable-gcov],
      [compile with coverage profiling instrumentation (gcc only)]),
    enable_gcov=$enableval,
    enable_gcov=no)
  if test x$enable_gcov = xyes ; then
    if test "x$GCC" != "xyes"
    then
      AC_MSG_ERROR([gcov only works if gcc is used])
    fi

    AS_COMPILER_FLAG(["-fprofile-arcs"],
      [GCOV_CFLAGS="$GCOV_CFLAGS -fprofile-arcs"],
      true)
    AS_COMPILER_FLAG(["-ftest-coverage"],
      [GCOV_CFLAGS="$GCOV_CFLAGS -ftest-coverage"],
      true)
    dnl remove any -O flags - FIXME: is this needed ?
    GCOV_CFLAGS=`echo "$GCOV_CFLAGS" | sed -e 's/-O[[0-9]]*//g'`
    dnl libtool 1.5.22 and lower strip -fprofile-arcs from the flags
    dnl passed to the linker, which is a bug; -fprofile-arcs implicitly
    dnl links in -lgcov, so we do it explicitly here for the same effect
    GCOV_LIBS=-lgcov
    AC_SUBST(GCOV_CFLAGS)
    AC_SUBST(GCOV_LIBS)
    GCOV=`echo $CC | sed s/gcc/gcov/g`
    AC_SUBST(GCOV)

    GST_GCOV_ENABLED=yes
    AC_DEFINE_UNQUOTED(GST_GCOV_ENABLED, 1,
      [Defined if gcov is enabled to force a rebuild due to config.h changing])
    dnl if gcov is used, we do not want default -O2 CFLAGS
    if test "x$GST_GCOV_ENABLED" = "xyes"
    then
      CFLAGS="-O0"
      AC_SUBST(CFLAGS)
      CXXFLAGS="-O0"
      AC_SUBST(CXXFLAGS)
      FFLAGS="-O0"
      AC_SUBST(FFLAGS)
      CCASFLAGS="-O0"
      AC_SUBST(CCASFLAGS)
      AC_MSG_NOTICE([gcov enabled, setting CFLAGS and friends to $CFLAGS])
    fi
  fi
  AM_CONDITIONAL(GST_GCOV_ENABLED, test x$enable_gcov = xyes)
])

AC_DEFUN([AG_GST_ARG_EXAMPLES],
[
  AC_ARG_ENABLE(examples,
    AC_HELP_STRING([--disable-examples], [disable building examples]),
      [
        case "${enableval}" in
          yes) BUILD_EXAMPLES=yes ;;
          no)  BUILD_EXAMPLES=no ;;
          *)   AC_MSG_ERROR(bad value ${enableval} for --disable-examples) ;;
        esac
      ],
      [BUILD_EXAMPLES=yes]) dnl Default value
  AM_CONDITIONAL(BUILD_EXAMPLES,      test "x$BUILD_EXAMPLES" = "xyes")
])

AC_DEFUN([AG_GST_ARG_WITH_PKG_CONFIG_PATH],
[
  dnl possibly modify pkg-config path
  AC_ARG_WITH(pkg-config-path, 
     AC_HELP_STRING([--with-pkg-config-path],
                    [colon-separated list of pkg-config(1) dirs]),
     [
       export PKG_CONFIG_PATH=${withval}
       AC_MSG_NOTICE(Set PKG_CONFIG_PATH to $PKG_CONFIG_PATH)
     ])
])


dnl This macro requires that GST_CVS is set to yes or no (release)
AC_DEFUN([AG_GST_ARG_WITH_PACKAGE_NAME],
[
  dnl package name in plugins
  AC_ARG_WITH(package-name,
    AC_HELP_STRING([--with-package-name],
      [specify package name to use in plugins]),
    [
      case "${withval}" in
        yes) AC_MSG_ERROR(bad value ${withval} for --with-package-name) ;;
        no)  AC_MSG_ERROR(bad value ${withval} for --with-package-name) ;;
        *)   GST_PACKAGE_NAME="${withval}" ;;
      esac
    ], 
    [
      P=$1
      if test "x$P" = "x"
      then
        P=$PACKAGE_NAME
      fi
      
      dnl default value
      if test "x$GST_CVS" = "xyes"
      then
        dnl nano >= 1
        GST_PACKAGE_NAME="$P CVS/prerelease"
      else
        GST_PACKAGE_NAME="$P source release"
      fi
    ]
  )
  AC_MSG_NOTICE(Using $GST_PACKAGE_NAME as package name)
  AC_DEFINE_UNQUOTED(GST_PACKAGE_NAME, "$GST_PACKAGE_NAME",
      [package name in plugins])
  AC_SUBST(GST_PACKAGE_NAME)
])

AC_DEFUN([AG_GST_ARG_WITH_PACKAGE_ORIGIN],
[
  dnl package origin URL
  AC_ARG_WITH(package-origin,
    AC_HELP_STRING([--with-package-origin],
      [specify package origin URL to use in plugins]),
    [
      case "${withval}" in
        yes) AC_MSG_ERROR(bad value ${withval} for --with-package-origin) ;;
        no)  AC_MSG_ERROR(bad value ${withval} for --with-package-origin) ;;
        *)   GST_PACKAGE_ORIGIN="${withval}" ;;
      esac
    ], 
    [GST_PACKAGE_ORIGIN="[Unknown package origin]"] dnl Default value
  )
  AC_MSG_NOTICE(Using $GST_PACKAGE_ORIGIN as package origin)
  AC_DEFINE_UNQUOTED(GST_PACKAGE_ORIGIN, "$GST_PACKAGE_ORIGIN",
      [package origin])
  AC_SUBST(GST_PACKAGE_ORIGIN)
])

dnl sets GST_PLUGINS_SELECTED to the list given as an argument, or to
dnl GST_PLUGINS_ALL
AC_DEFUN([AG_GST_ARG_WITH_PLUGINS],
[
  AC_ARG_WITH(plugins,
    AC_HELP_STRING([--with-plugins],
      [comma-separated list of dependencyless plug-ins to compile]),
    [
      for i in `echo $withval | tr , ' '`; do
        if echo $GST_PLUGINS_ALL | grep $i > /dev/null
        then
            GST_PLUGINS_SELECTED="$GST_PLUGINS_SELECTED $i"
        else
            echo "plug-in $i not recognized, ignoring..."
        fi
    done],
    [GST_PLUGINS_SELECTED=$GST_PLUGINS_ALL])
])

AC_DEFUN([AG_GST_ARG_ENABLE_EXTERNAL],
[
  AG_GST_CHECK_FEATURE(EXTERNAL, [enable building of plug-ins with external deps],,
    HAVE_EXTERNAL=yes, enabled,
    [
      AC_MSG_NOTICE(building external plug-ins)
      BUILD_EXTERNAL="yes"
    ],[
      AC_MSG_WARN(all plug-ins with external dependencies will not be built)
      BUILD_EXTERNAL="no"
    ])
  # make BUILD_EXTERNAL available to Makefile.am
  AM_CONDITIONAL(BUILD_EXTERNAL, test "x$BUILD_EXTERNAL" = "xyes")
])

dnl experimental plug-ins; stuff that hasn't had the dust settle yet
dnl read 'builds, but might not work'
AC_DEFUN([AG_GST_ARG_ENABLE_EXPERIMENTAL],
[
  AG_GST_CHECK_FEATURE(EXPERIMENTAL,
    [building of experimental plug-ins],,
    HAVE_EXPERIMENTAL=yes, disabled,
    [
      AC_MSG_WARN(building experimental plug-ins)
      BUILD_EXPERIMENTAL="yes"
    ],[
      AC_MSG_NOTICE(not building experimental plug-ins)
      BUILD_EXPERIMENTAL="no"
    ])
  # make BUILD_EXPERIMENTAL available to Makefile.am
  AM_CONDITIONAL(BUILD_EXPERIMENTAL, test "x$BUILD_EXPERIMENTAL" = "xyes")
])

dnl broken plug-ins; stuff that doesn't seem to build at the moment
AC_DEFUN([AG_GST_ARG_ENABLE_BROKEN],
[
  AG_GST_CHECK_FEATURE(BROKEN, [enable building of broken plug-ins],,
    HAVE_BROKEN=yes, disabled,
    [
      AC_MSG_WARN([building broken plug-ins -- no bug reports on these, only patches ...])
    ],[
      AC_MSG_NOTICE([not building broken plug-ins])
    ])
])
