dnl ##
dnl ##  NGPT - Next Generation POSIX Threading
dnl ##  Copyright (c) 2001 IBM Corporation <babt@us.ibm.com>
dnl ##  Copyright (c) 1999-2000 Ralf S. Engelschall <rse@engelschall.com>
dnl ##
dnl ##  This file is part of NGPT, a non-preemptive thread scheduling
dnl ##  library which can be found at http://www.ibm.com/developer
dnl ##
dnl ##  This library is free software; you can redistribute it and/or
dnl ##  modify it under the terms of the GNU Lesser General Public
dnl ##  License as published by the Free Software Foundation; either
dnl ##  version 2.1 of the License, or (at your option) any later version.
dnl ##
dnl ##  This library is distributed in the hope that it will be useful,
dnl ##  but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl ##  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
dnl ##  Lesser General Public License for more details.
dnl ##
dnl ##  You should have received a copy of the GNU Lesser General Public
dnl ##  License along with this library; if not, write to the Free Software
dnl ##  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
dnl ##  USA, or contact Bill Abt <babt@us.ibm.com>
dnl ##
dnl ##  aclocal.m4: Pth Autoconf macros
dnl ##
                        dnl # ``"Reuse an expert's code" is the right
                        dnl #   advice for most people. But it's a useless 
                        dnl #   advice for the experts writing the code 
                        dnl #   in the first place.'
                        dnl #               -- Dan J. Bernstein

dnl ##
dnl ##  Display Configuration Headers
dnl ##
dnl ##  configure.in:
dnl ##    AC_MSG_PART(<text>)
dnl ##

define(AC_MSG_PART,[dnl
if test ".$enable_subdir" != .yes; then
    AC_MSG_RESULT()
    AC_MSG_RESULT(${TB}$1:${TN})
fi
])dnl

dnl ##
dnl ##  Display a message under --verbose
dnl ##
dnl ##  configure.in:
dnl ##    AC_MSG_VERBOSE(<text>)
dnl ##

define(AC_MSG_VERBOSE,[dnl
if test ".$verbose" = .yes; then
    AC_MSG_RESULT([  $1])
fi
])

dnl ##
dnl ##  Do not display message for a command
dnl ##
dnl ##  configure.in:
dnl ##    AC_MSG_SILENT(...)
dnl ##

define(AC_FD_TMP, 9)
define(AC_MSG_SILENT,[dnl
exec AC_FD_TMP>&AC_FD_MSG AC_FD_MSG>/dev/null
$1
exec AC_FD_MSG>&AC_FD_TMP AC_FD_TMP>&-
])

dnl ##
dnl ##  Perform something only once
dnl ##
dnl ##  configure.in:
dnl ##    AC_ONCE(<action>)
dnl ##

define(AC_ONCE,[
ifelse(ac_once_$1, already_done, ,[
    define(ac_once_$1, already_done)
    $2
])dnl
])

dnl ##
dnl ##  Support for $(S)
dnl ##
dnl ##  configure.in:
dnl ##    AC_SRCDIR_PREFIX(<varname>)
dnl ##

AC_DEFUN(AC_SRCDIR_PREFIX,[
ac_prog=[$]0
changequote(, )dnl
ac_srcdir=`echo $ac_prog | sed -e 's%/[^/][^/]*$%%' -e 's%\([^/]\)/*$%\1%'`
changequote([, ])dnl
if test ".$ac_srcdir" = ".$ac_prog"; then
    ac_srcdir=""
elif test "x$ac_srcdir" = "x."; then
    ac_srcdir=""
else
    if test ".$CFLAGS" = .; then
        CFLAGS="-I$ac_srcdir"
    else
        CFLAGS="$CFLAGS -I$ac_srcdir"
    fi
    ac_srcdir="$ac_srcdir/"
fi
$1="$ac_srcdir"
AC_SUBST($1)
])dnl

dnl ##
dnl ##  Support for --enable-subdir (for use with pth.m4)
dnl ##
dnl ##  configure.in:
dnl ##    AC_ENABLESUBDIR
dnl ##

AC_DEFUN(AC_ENABLESUBDIR,[
AC_ARG_ENABLE(subdir,dnl
[  --enable-subdir         enable local building as subdirectory (default=no)],[dnl
],[dnl
enable_subdir=no
])dnl
if test ".$enable_subdir" = .yes; then
    enable_batch=yes
    enable_shared=no
fi
])dnl

dnl ##
dnl ##  Support for Configuration Headers
dnl ##
dnl ##  configure.in:
dnl ##    AC_HEADLINE(<short-name>, <long-name>,
dnl ##                <vers-var>, <vers-file>,
dnl ##                <copyright>)
dnl ##

AC_DEFUN(AC_HEADLINE,[dnl
AC_DIVERT_PUSH(AC_DIVERSION_NOTICE)dnl
#   configuration header
if test ".`echo dummy [$]@ | grep enable-subdir`" != .; then
    enable_subdir=yes
fi
if test ".`echo dummy [$]@ | grep help`" = .; then
    #   bootstrapping shtool
    ac_prog=[$]0
changequote(, )dnl
    ac_srcdir=`echo $ac_prog | sed -e 's%/[^/][^/]*$%%' -e 's%\([^/]\)/*$%\1%'`
changequote([, ])dnl
    test ".$ac_srcdir" = ".$ac_prog" && ac_srcdir=.
    ac_shtool="${CONFIG_SHELL-/bin/sh} $ac_srcdir/shtool"

    #   find out terminal sequences
    if test ".$enable_subdir" != .yes; then
        TB=`$ac_shtool echo -n -e %B 2>/dev/null`
        TN=`$ac_shtool echo -n -e %b 2>/dev/null`
    else
        TB=''
        TN=''
    fi

    #   find out package version
    $3_STR="`$ac_shtool version -lc -dlong $ac_srcdir/$4`"
    AC_SUBST($3_STR)

    #   friendly header ;)
    if test ".$enable_subdir" != .yes; then
        echo "Configuring ${TB}$1${TN} ($2), Version ${TB}${$3_STR}${TN}"
        echo "$5"
    fi

    #   additionally find out hex version
    $3_HEX="`$ac_shtool version -lc -dhex $ac_srcdir/$4`"
    AC_SUBST($3_HEX)
fi
AC_DIVERT_POP()
])dnl

dnl ##
dnl ##  Support for Platform IDs
dnl ##
dnl ##  configure.in:
dnl ##    AC_PLATFORM(<variable>)
dnl ##

AC_DEFUN(AC_PLATFORM,[
if test ".$host" != .NONE; then
    $1="$host"
elif test ".$nonopt" != .NONE; then
    $1="$nonopt"
else
    $1=`${CONFIG_SHELL-/bin/sh} $srcdir/config.guess`
fi
$1=`${CONFIG_SHELL-/bin/sh} $srcdir/config.sub $$1` || exit 1
AC_SUBST($1)
if test ".$enable_subdir" != .yes; then
    echo "Platform: ${TB}${$1}${TN}"
fi
])dnl

dnl ##
dnl ##  Support for config.param files
dnl ##
dnl ##  configure.in:
dnl ##    AC_CONFIG_PARAM(<file>)
dnl ##

AC_DEFUN(AC_CONFIG_PARAM,[
AC_DIVERT_PUSH(-1)
AC_ARG_WITH(param,[  --with-param=ID[,ID,..] load parameters from $1])
AC_DIVERT_POP()
AC_DIVERT_PUSH(AC_DIVERSION_NOTICE)
ac_prev=""
ac_param=""
if test -f $1; then
    ac_param="$1:common"
fi
for ac_option
do
    if test ".$ac_prev" != .; then
        eval "$ac_prev=\$ac_option"
        ac_prev=""
        continue
    fi
    case "$ac_option" in
        -*=*) ac_optarg=`echo "$ac_option" | sed 's/[[-_a-zA-Z0-9]]*=//'` ;;
           *) ac_optarg="" ;;
    esac
    case "$ac_option" in
        --with-param=* )
            case $ac_optarg in
                *:* )
                    ac_from=`echo $ac_optarg | sed -e 's/:.*//'`
                    ac_what=`echo $ac_optarg | sed -e 's/.*://'`
                    ;;
                * )
                    ac_from="$1"
                    ac_what="$ac_optarg"
                    ;;
            esac
            if test ".$ac_param" = .; then
                ac_param="$ac_from:$ac_what"
            else
                ac_param="$ac_param,$ac_from:$ac_what"
            fi
            ;;
    esac
done
if test ".$ac_param" != .; then
    # echo "loading parameters"
    OIFS="$IFS"
    IFS=","
    pconf="/tmp/autoconf.$$"
    echo "ac_options=''" >$pconf
    ac_from="$1"
    for ac_section in $ac_param; do
        changequote(, )
        case $ac_section in
            *:* )
                ac_from=`echo "$ac_section" | sed -e 's/:.*//'`
                ac_section=`echo "$ac_section" | sed -e 's/.*://'`
                ;;
        esac
        (echo ''; cat $ac_from; echo '') |\
        sed -e "1,/[ 	]*[ 	]*${ac_section}[ 	]*{[ 	]*/d" \
            -e '/[ 	]*}[ 	]*/,$d' \
            -e ':join' -e '/\\[ 	]*$/N' -e 's/\\[ 	]*\n[ 	]*//' -e 'tjoin' \
            -e 's/^[ 	]*//g' \
            -e 's/^\([^-].*=.*\) IF \(.*\)$/if \2; then \1; fi/' \
            -e 's/^\(--.*=.*\) IF \(.*\)$/if \2; then ac_options="$ac_options \1"; fi/' \
            -e 's/^\(--.*\) IF \(.*\)$/if \2; then ac_options="$ac_options \1"; fi/' \
            -e 's/^\(--.*=.*\)$/ac_options="$ac_options \1"/' \
            -e 's/^\(--.*\)$/ac_options="$ac_options \1"/' \
            >>$pconf
        changequote([, ])
    done
    IFS="$OIFS"
    . $pconf
    rm -f $pconf >/dev/null 2>&1
    if test ".[$]*" = .; then
        set -- $ac_options
    else
        set -- "[$]@" $ac_options
    fi
fi
AC_DIVERT_POP()
])dnl

dnl ##
dnl ##  Check whether compiler option works
dnl ##
dnl ##  configure.in:
dnl ##    AC_COMPILER_OPTION(<name>, <display>, <option>,
dnl ##                       <action-success>, <action-failure>)
dnl ##

AC_DEFUN(AC_COMPILER_OPTION,[dnl
AC_MSG_CHECKING(for compiler option $2)
AC_CACHE_VAL(ac_cv_compiler_option_$1,[
cat >conftest.$ac_ext <<EOF
int main() { return 0; }
EOF
${CC-cc} -c $CFLAGS $CPPFLAGS $3 conftest.$ac_ext 1>conftest.out 2>conftest.err
if test $? -ne 0 -o -s conftest.err; then
     ac_cv_compiler_option_$1=no
else
     ac_cv_compiler_option_$1=yes
fi
rm -f conftest.$ac_ext conftest.out conftest.err
])dnl
if test ".$ac_cv_compiler_option_$1" = .yes; then
    ifelse([$4], , :, [$4])
else
    ifelse([$5], , :, [$5])
fi
AC_MSG_RESULT([$ac_cv_compiler_option_$1])
])dnl

dnl ##
dnl ##  Check whether `long long' and `long double' type exists
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_LONGLONG
dnl ##    AC_CHECK_LONGDOUBLE
dnl ##

AC_DEFUN(AC_CHECK_LONGLONG,[dnl
AC_MSG_CHECKING(for built-in type long long)
 AC_CACHE_VAL(ac_cv_type_longlong,[
  AC_TRY_LINK([
#include <sys/types.h>
  ],[
    long long X = 2, Y = 1, Z;
    Z = X / Y; 
  ],
    AC_DEFINE(HAVE_LONGLONG)
    ac_cv_type_longlong=yes, 
    ac_cv_type_longlong=no
  )dnl
 ])dnl
AC_MSG_RESULT([$ac_cv_type_longlong])
])dnl

AC_DEFUN(AC_CHECK_LONGDOUBLE,[dnl
AC_MSG_CHECKING(for built-in type long double)
 AC_CACHE_VAL(ac_cv_type_longdouble,[
  AC_TRY_LINK([
#include <sys/types.h>
  ],[
    long double X = 2, Y = 1, Z;
    Z = X / Y; 
  ],
    AC_DEFINE(HAVE_LONGDOUBLE)
    ac_cv_type_longdouble=yes, 
    ac_cv_type_longdouble=no
  )dnl
 ])dnl
AC_MSG_RESULT([$ac_cv_type_longdouble])
])dnl

dnl ##
dnl ##  Minimalistic Libtool Glue Code
dnl ##
dnl ##  configure.in:
dnl ##    AC_PROG_LIBTOOL(<platform-variable>)
dnl ##

AC_DEFUN(AC_PROG_LIBTOOL,[dnl
AC_ARG_ENABLE(static,dnl
[  --enable-static         build static libraries (default=yes)],
enable_static="$enableval",
if test ".$enable_static" = .; then
    enable_static=yes
fi
)dnl
AC_ARG_ENABLE(shared,dnl
[  --enable-shared         build shared libraries (default=yes)],
enable_shared="$enableval",
if test ".$enable_shared" = .; then
    enable_shared=yes
fi
)dnl
libtool_flags=''
dnl libtool_flags="$libtool_flags --cache-file=$cache_file"
test ".$silent"            = .yes && libtool_flags="$libtool_flags --silent"
test ".$enable_static"     = .no  && libtool_flags="$libtool_flags --disable-static"
test ".$enable_shared"     = .no  && libtool_flags="$libtool_flags --disable-shared"
test ".$ac_cv_prog_gcc"    = .yes && libtool_flags="$libtool_flags --with-gcc"
test ".$ac_cv_prog_gnu_ld" = .yes && libtool_flags="$libtool_flags --with-gnu-ld"
CC="$CC" CFLAGS="$CFLAGS" CPPFLAGS="$CPPFLAGS" LD="$LD" \
${CONFIG_SHELL-/bin/sh} $srcdir/ltconfig --no-reexec \
$libtool_flags --srcdir=$srcdir --no-verify $srcdir/ltmain.sh $1 ||\
AC_MSG_ERROR([libtool configuration failed])
dnl (AC_CACHE_LOAD) >/dev/null 2>&1
])dnl

dnl ##
dnl ##  Disable kernel patch warning
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_KERNEL_PATCH_WARNING
dnl ##

AC_DEFUN(AC_CHECK_KERNEL_PATCH_WARNING,[dnl
AC_ARG_ENABLE(kernel-patch,dnl
[  --disable-kernel-patch-warning   disable kernel patch warning(default=no)],
[dnl
AC_DEFINE(PTH_NO_PATCH_WARNING)
msg="disabled"
],[
msg="enabled"
])dnl
AC_MSG_CHECKING(for 2.4 kernel patch warning)
AC_MSG_RESULT([$msg])
])

dnl ##
dnl ##  Verbose Debugging Support
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_VERBOSE_DEBUGGING
dnl ##

AC_DEFUN(AC_CHECK_VERBOSE_DEBUGGING,[dnl
AC_ARG_ENABLE(verbose-debug,dnl
[  --enable-verbose-debug          build for verbose debugging (default=no)],
[dnl
if test ".$ac_cv_prog_gcc" = ".yes"; then
    case "$CFLAGS" in
        *-O* ) ;;
           * ) CFLAGS="$CFLAGS -O2" ;;
    esac
    case "$CFLAGS" in
        *-g* ) ;;
           * ) CFLAGS="$CFLAGS -g" ;;
    esac
    case "$CFLAGS" in
        *-pipe* ) ;;
              * ) AC_COMPILER_OPTION(pipe, -pipe, -pipe, CFLAGS="$CFLAGS -pipe") ;;
    esac
    AC_COMPILER_OPTION(ggdb3, -ggdb3, -ggdb3, CFLAGS="$CFLAGS -ggdb3")
    case $PLATFORM in
        *-*-freebsd*|*-*-solaris* ) CFLAGS="$CFLAGS -pedantic" ;;
    esac
    CFLAGS="$CFLAGS -Wall"
    WMORE="-Wshadow -Wpointer-arith -Wcast-align -Winline -Wno-unused-function"
    WMORE="$WMORE -Wmissing-prototypes -Wmissing-declarations -Wnested-externs"
    AC_COMPILER_OPTION(wmore, -W<xxx>, $WMORE, CFLAGS="$CFLAGS $WMORE")
    AC_COMPILER_OPTION(wnolonglong, -Wno-long-long, -Wno-long-long, CFLAGS="$CFLAGS -Wno-long-long")
else
    case "$CFLAGS" in
        *-g* ) ;;
           * ) CFLAGS="$CFLAGS -g" ;;
    esac
fi
msg="enabled"
AC_DEFINE(PTH_DEBUG)
],[
if test ".$ac_cv_prog_gcc" = ".yes"; then
case "$CFLAGS" in
    *-pipe* ) ;;
          * ) AC_COMPILER_OPTION(pipe, -pipe, -pipe, CFLAGS="$CFLAGS -pipe") ;;
esac
fi
case "$CFLAGS" in
    *-g* ) CFLAGS=`echo "$CFLAGS" |\
                   sed -e 's/ -g / /g' -e 's/ -g$//' -e 's/^-g //g' -e 's/^-g$//'` ;;
esac
case "$CXXFLAGS" in
    *-g* ) CXXFLAGS=`echo "$CXXFLAGS" |\
                     sed -e 's/ -g / /g' -e 's/ -g$//' -e 's/^-g //g' -e 's/^-g$//'` ;;
esac
msg="disabled"
])dnl
AC_MSG_CHECKING(for compilation debug mode)
AC_MSG_RESULT([$msg])
    case $PLATFORM in
        *-*-mvs* ) 
	    # Don't turn off shared libaries on OS/390.
	    ;;
	* )
	    if test ".$msg" = .enabled; then
	      enable_shared=no
	    fi
	    ;;
	esac
])

dnl ##
dnl ##  Standard Debugging Support
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_STD_DEBUGGING
dnl ##

AC_DEFUN(AC_CHECK_STD_DEBUGGING,[dnl
AC_ARG_ENABLE(debug,dnl
[  --enable-debug          build for debugging (default=no)],
[dnl
if test ".$ac_cv_prog_gcc" = ".yes"; then
    case "$CFLAGS" in
        *-O* ) ;;
           * ) CFLAGS="$CFLAGS -O2" ;;
    esac
    case "$CFLAGS" in
        *-g* ) ;;
           * ) CFLAGS="$CFLAGS -g" ;;
    esac
    case "$CFLAGS" in
        *-pipe* ) ;;
              * ) AC_COMPILER_OPTION(pipe, -pipe, -pipe, CFLAGS="$CFLAGS -pipe") ;;
    esac
    AC_COMPILER_OPTION(ggdb3, -ggdb3, -ggdb3, CFLAGS="$CFLAGS -ggdb3")
    case $PLATFORM in
        *-*-freebsd*|*-*-solaris* ) CFLAGS="$CFLAGS -pedantic" ;;
    esac
    CFLAGS="$CFLAGS -DASSERTS_ON -Wall"
    WMORE="-Wshadow -Wpointer-arith -Wcast-align -Winline -Wno-unused-function"
    WMORE="$WMORE -Wmissing-prototypes -Wmissing-declarations -Wnested-externs"
    AC_COMPILER_OPTION(wmore, -W<xxx>, $WMORE, CFLAGS="$CFLAGS $WMORE")
    AC_COMPILER_OPTION(wnolonglong, -Wno-long-long, -Wno-long-long, CFLAGS="$CFLAGS -Wno-long-long")
else
    case "$CFLAGS" in
        *-g* ) ;;
           * ) CFLAGS="$CFLAGS -g" ;;
    esac
fi
msg="enabled"
],[
if test ".$ac_cv_prog_gcc" = ".yes"; then
case "$CFLAGS" in
    *-pipe* ) ;;
          * ) AC_COMPILER_OPTION(pipe, -pipe, -pipe, CFLAGS="$CFLAGS -pipe") ;;
esac
fi
case "$CFLAGS" in
    *-g* ) CFLAGS=`echo "$CFLAGS" |\
                   sed -e 's/ -g / /g' -e 's/ -g$//' -e 's/^-g //g' -e 's/^-g$//'` ;;
esac
case "$CXXFLAGS" in
    *-g* ) CXXFLAGS=`echo "$CXXFLAGS" |\
                     sed -e 's/ -g / /g' -e 's/ -g$//' -e 's/^-g //g' -e 's/^-g$//'` ;;
esac
msg="disabled"
])dnl
AC_MSG_CHECKING(for compilation debug mode)
AC_MSG_RESULT([$msg])
    case $PLATFORM in
	* )
	    ;;
	esac
])

dnl ##
dnl ##  Profiling Support
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_PROFILING
dnl ##

AC_DEFUN(AC_CHECK_PROFILING,[dnl
AC_MSG_CHECKING(for compilation profile mode)
AC_ARG_ENABLE(profile,dnl
[  --enable-profile        build for profiling (default=no)],
[dnl
if test ".$ac_cv_prog_gcc" = ".no"; then
    AC_MSG_ERROR([profiling requires gcc and gprof])
fi
CFLAGS=`echo "$CFLAGS" | sed -e 's/-O2//g'`
CFLAGS="$CFLAGS -O0 -pg"
LDFLAGS="$LDFLAGS -pg"
msg="enabled"
],[
msg="disabled"
])dnl
AC_MSG_RESULT([$msg])
if test ".$msg" = .enabled; then
    enable_shared=no
fi
])

dnl ##
dnl ##  Build Parameters
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_BUILDPARAM
dnl ##

AC_DEFUN(AC_CHECK_BUILDPARAM,[dnl
dnl #   determine build mode
AC_MSG_CHECKING(whether to activate batch build mode)
AC_ARG_ENABLE(batch,dnl
[  --enable-batch          enable batch build mode (default=no)],[dnl
],[dnl
enable_batch=no
])dnl
if test ".$silent" = .yes; then
    enable_batch=yes
fi
AC_MSG_RESULT([$enable_batch])
BATCH="$enable_batch"
AC_SUBST(BATCH)
dnl #   determine build targets
TARGET_ALL='$(TARGET_PREQ) $(TARGET_LIBS)'
AC_MSG_CHECKING(whether to activate maintainer build targets)
AC_ARG_ENABLE(maintainer,dnl
[  --enable-maintainer     enable maintainer build targets (default=no)],[dnl
],[dnl
enable_maintainer=no
])dnl
AC_MSG_RESULT([$enable_maintainer])
if test ".$enable_maintainer" = .yes; then
    TARGET_ALL="$TARGET_ALL \$(TARGET_MANS)"
fi
AC_MSG_CHECKING(whether to activate test build targets)
AC_ARG_ENABLE(tests,dnl
[  --enable-tests          enable test build targets (default=yes)],[dnl
],[dnl
enable_tests=yes
])dnl
AC_MSG_RESULT([$enable_tests])
if test ".$enable_tests" = .yes; then
    TARGET_ALL="$TARGET_ALL \$(TARGET_TEST)"
fi
AC_SUBST(TARGET_ALL)
])

dnl ##
dnl ##  Optimization Support
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_OPTIMIZE
dnl ##

AC_DEFUN(AC_CHECK_OPTIMIZE,[dnl
AC_ARG_ENABLE(optimize,dnl
[  --enable-optimize       build with optimization (default=no)],
[dnl
if test ".$ac_cv_prog_gcc" = ".yes"; then
    #  compiler is gcc
    case "$CFLAGS" in
        *-O* ) ;;
        * ) CFLAGS="$CFLAGS -O2" ;;
    esac
    case "$CFLAGS" in
        *-pipe* ) ;;
        * ) AC_COMPILER_OPTION(pipe, -pipe, -pipe, CFLAGS="$CFLAGS -pipe") ;;
    esac
    OPT_CFLAGS='-funroll-loops -fstrength-reduce -fomit-frame-pointer -ffast-math'
    AC_COMPILER_OPTION(optimize_std, [-f<xxx> for optimizations], $OPT_CFLAGS, CFLAGS="$CFLAGS $OPT_CFLAGS")
    case $PLATFORM in
        i?86*-*-*|?86*-*-* )
            OPT_CFLAGS='-malign-functions=4 -malign-jumps=4 -malign-loops=4' 
            AC_COMPILER_OPTION(optimize_x86, [-f<xxx> for Intel x86 CPU], $OPT_CFLAGS, CFLAGS="$CFLAGS $OPT_CFLAGS")
            ;;
    esac
else
    #  compiler is NOT gcc
    case "$CFLAGS" in
        *-O* ) ;;
           * ) CFLAGS="$CFLAGS -O" ;;
    esac
    case $PLATFORM in
        *-*-solaris* )
            AC_COMPILER_OPTION(fast, -fast, -fast, CFLAGS="$CFLAGS -fast")
            ;;
    esac
fi
msg="enabled"
],[
msg="disabled"
])dnl
AC_MSG_CHECKING(for compilation optimization mode)
AC_MSG_RESULT([$msg])
])

dnl ##
dnl ##  Check for a pre-processor define in a header
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_DEFINE(<define>, <header>)
dnl ##  acconfig.h:
dnl ##    #undef HAVE_<define>
dnl ##

AC_DEFUN(AC_CHECK_DEFINE,[dnl
AC_MSG_CHECKING(for define $1 in $2)
AC_CACHE_VAL(ac_cv_define_$1,
[AC_EGREP_CPP([YES_IS_DEFINED], [
#include <$2>
#ifdef $1
YES_IS_DEFINED
#endif
], ac_cv_define_$1=yes, ac_cv_define_$1=no)])dnl
AC_MSG_RESULT($ac_cv_define_$1)
if test $ac_cv_define_$1 = yes; then
    AC_DEFINE(HAVE_[]translit($1, [a-z], [A-Z]))
fi
])

dnl ##
dnl ##  Check for an ANSI C typedef in a header
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_TYPEDEF(<typedef>, <header>)
dnl ##  acconfig.h:
dnl ##    #undef HAVE_<typedef>
dnl ##

AC_DEFUN(AC_CHECK_TYPEDEF,[dnl
AC_REQUIRE([AC_HEADER_STDC])dnl
AC_MSG_CHECKING(for typedef $1)
AC_CACHE_VAL(ac_cv_typedef_$1,
[AC_EGREP_CPP(dnl
changequote(<<,>>)dnl
<<(^|[^a-zA-Z_0-9])$1[^a-zA-Z_0-9]>>dnl
changequote([,]), [
#include <$2>
], ac_cv_typedef_$1=yes, ac_cv_typedef_$1=no)])dnl
AC_MSG_RESULT($ac_cv_typedef_$1)
if test $ac_cv_typedef_$1 = yes; then
    AC_DEFINE(HAVE_[]translit($1, [a-z], [A-Z]))
fi
])

dnl ##
dnl ##  Check for an ANSI C struct attribute in a structure defined in a header
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_STRUCTATTR(<attr>, <struct>, <header>)
dnl ##  acconfig.h:
dnl ##    #undef HAVE_<attr>
dnl ##

AC_DEFUN(AC_CHECK_STRUCTATTR,[dnl
AC_REQUIRE([AC_HEADER_STDC])dnl
AC_MSG_CHECKING(for attribute $1 in struct $2 from $3)
AC_CACHE_VAL(ac_cv_structattr_$1,[dnl

AC_TRY_LINK([
#include <sys/types.h>
#include <$3>
],[
struct $2 *sp1;
struct $2 *sp2;
sp1->$1 = sp2->$1;
], ac_cv_structattr_$1=yes, ac_cv_structattr_$1=no)])dnl
AC_MSG_RESULT($ac_cv_structattr_$1)
if test $ac_cv_structattr_$1 = yes; then
    AC_DEFINE(HAVE_[]translit($1, [a-z], [A-Z]))
fi
])

dnl ##
dnl ##  Check for argument type of a function
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_ARGTYPE(<header> [...], <func>, <arg-number>,
dnl ##                     <max-arg-number>, <action-with-${ac_type}>)
dnl ##

AC_DEFUN(AC_CHECK_ARGTYPE,[dnl
AC_REQUIRE_CPP()dnl
AC_MSG_CHECKING([for type of argument $3 for $2()])
AC_CACHE_VAL([ac_cv_argtype_$2$3],[
cat >conftest.$ac_ext <<EOF
[#]line __oline__ "configure"
#include "confdefs.h"
EOF
for ifile in $1; do
    echo "#include <$ifile>" >>conftest.$ac_ext
done
gpat=''
spat=''
i=1
changequote(, )dnl
while test $i -le $4; do
    gpat="$gpat[^,]*"
    if test $i -eq $3; then
        spat="$spat\\([^,]*\\)"
    else
        spat="$spat[^,]*"
    fi
    if test $i -lt $4; then
        gpat="$gpat,"
        spat="$spat,"
    fi
    i=`expr $i + 1`
done
(eval "$ac_cpp conftest.$ac_ext") 2>&AC_FD_CC |\
sed -e ':join' \
    -e '/,[ 	]*$/N' \
    -e 's/,[ 	]*\n[ 	]*/, /' \
    -e 'tjoin' |\
egrep "[^a-zA-Z0-9_]$2[ 	]*\\($gpat\\)" | head -1 |\
sed -e "s/.*[^a-zA-Z0-9_]$2[ 	]*($spat).*/\\1/" \
    -e 's/(\*[a-zA-Z_][a-zA-Z_0-9]*)/(*)/' \
    -e 's/^[ 	]*//' -e 's/[ 	]*$//' \
    -e 's/^/arg:/' \
    -e 's/^arg:\([^ 	]*\)$/type:\1/' \
    -e 's/^arg:\(.*_t\)*$/type:\1/' \
    -e 's/^arg:\(.*\*\)$/type:\1/' \
    -e 's/^arg:\(.*[ 	]\*\)[_a-zA-Z][_a-zA-Z0-9]*$/type:\1/' \
    -e 's/^arg:\(.*[ 	]char\)$/type:\1/' \
    -e 's/^arg:\(.*[ 	]short\)$/type:\1/' \
    -e 's/^arg:\(.*[ 	]int\)$/type:\1/' \
    -e 's/^arg:\(.*[ 	]long\)$/type:\1/' \
    -e 's/^arg:\(.*[ 	]float\)$/type:\1/' \
    -e 's/^arg:\(.*[ 	]double\)$/type:\1/' \
    -e 's/^arg:\(.*[ 	]unsigned\)$/type:\1/' \
    -e 's/^arg:\(.*[ 	]signed\)$/type:\1/' \
    -e 's/^arg:\(.*struct[ 	][_a-zA-Z][_a-zA-Z0-9]*\)$/type:\1/' \
    -e 's/^arg:\(.*\)[ 	]_[_a-zA-Z0-9]*$/type:\1/' \
    -e 's/^arg:\(.*\)[ 	]\([^ 	]*\)$/type:\1/' \
    -e 's/^type://' >conftest.output
ac_cv_argtype_$2$3=`cat conftest.output`
changequote([, ])dnl
rm -f conftest*
])
AC_MSG_RESULT([$ac_cv_argtype_$2$3])
ac_type="$ac_cv_argtype_$2$3"
[$5]
])

dnl ##
dnl ##  Check for existance of a function
dnl ##
dnl ##  configure.in:
dnl ##     AC_CHECK_FUNCTION(<function> [, <action-if-found> [, <action-if-not-found>]])
dnl ##     AC_CHECK_FUNCTIONS(<function> [...] [, <action-if-found> [, <action-if-not-found>]])
dnl ##

AC_DEFUN(AC_CHECK_FUNCTIONS,[dnl
for ac_func in $1; do
    AC_CHECK_FUNCTION($ac_func,
        [changequote(, )dnl
        ac_tr_func=HAVE_`echo $ac_func | tr 'abcdefghijklmnopqrstuvwxyz' 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'`
        changequote([, ])dnl
        AC_DEFINE_UNQUOTED($ac_tr_func) $2], $3)dnl
done
])

AC_DEFUN(AC_CHECK_FUNCTION, [dnl
AC_MSG_CHECKING([for function $1])
AC_CACHE_VAL(ac_cv_func_$1, [dnl
AC_TRY_LINK([
/* System header to define __stub macros and hopefully few prototypes,
   which can conflict with char $1(); below. */
#include <assert.h>
#ifdef __cplusplus
extern "C"
#endif
/* We use char because int might match the return type of a gcc2
   builtin and then its argument prototype would still apply. */
char $1();
char (*f)();
], [
/* The GNU C library defines this for functions which it implements
   to always fail with ENOSYS.  Some functions are actually named
   something starting with __ and the normal name is an alias. */
#if defined (__stub_$1) || defined (__stub___$1)
choke me
#else
f = $1;
#endif
], eval "ac_cv_func_$1=yes", eval "ac_cv_func_$1=no")])
if eval "test \"`echo '$ac_cv_func_'$1`\" = yes"; then
  AC_MSG_RESULT(yes)
  ifelse([$2], , :, [$2])
else
  AC_MSG_RESULT(no)
ifelse([$3], , , [$3
])dnl
fi
])

dnl ##
dnl ##  Decision Hierachy
dnl ##

define(AC_IFALLYES,[dnl
ac_rc=yes
for ac_spec in $1; do
    ac_type=`echo "$ac_spec" | sed -e 's/:.*$//'`
    ac_item=`echo "$ac_spec" | sed -e 's/^.*://'`
    case $ac_type in
        header [)]
            ac_item=`echo "$ac_item" | sed 'y%./+-%__p_%'`
            ac_var="ac_cv_header_$ac_item"
            ;;
        file [)]
            ac_item=`echo "$ac_item" | sed 'y%./+-%__p_%'`
            ac_var="ac_cv_file_$ac_item"
            ;;
        func    [)] ac_var="ac_cv_func_$ac_item"   ;;
        lib     [)] ac_var="ac_cv_lib_$ac_item"    ;;
        define  [)] ac_var="ac_cv_define_$ac_item" ;;
        typedef [)] ac_var="ac_cv_typedef_$ac_item" ;;
        custom  [)] ac_var="$ac_item" ;;
    esac
    eval "ac_val=\$$ac_var"
    if test ".$ac_val" != .yes; then
        ac_rc=no
        break
    fi
done
if test ".$ac_rc" = .yes; then
    :
    $2
else
    :
    $3
fi
])

define(AC_BEGIN_DECISION,[dnl
ac_decision_item='$1'
ac_decision_msg='FAILED'
ac_decision=''
])

define(AC_DECIDE,[dnl
ac_decision='$1'
ac_decision_msg='$2'
ac_decision_$1=yes
ac_decision_$1_msg='$2'
])

define(AC_DECISION_OVERRIDE,[dnl
    ac_decision=''
    for ac_item in $1; do
         eval "ac_decision_this=\$ac_decision_${ac_item}"
         if test ".$ac_decision_this" = .yes; then
             ac_decision=$ac_item
             eval "ac_decision_msg=\$ac_decision_${ac_item}_msg"
         fi
    done
])

define(AC_DECISION_FORCE,[dnl
ac_decision="$1"
eval "ac_decision_msg=\"\$ac_decision_${ac_decision}_msg\""
])

define(AC_END_DECISION,[dnl
if test ".$ac_decision" = .; then
    echo "[$]0:Error: decision on $ac_decision_item failed." 1>&2
    echo "[$]0:Hint: see config.log for more details!" 1>&2
    exit 1
else
    if test ".$ac_decision_msg" = .; then
        ac_decision_msg="$ac_decision"
    fi
    AC_MSG_RESULT([decision on $ac_decision_item... ${TB}$ac_decision_msg${TN}])
fi
])

dnl ##
dnl ##  Check for existance of a file
dnl ##
dnl ##  configure.in:
dnl ##    AC_TEST_FILE(<file>, <success-action>, <failure-action>)
dnl ##

AC_DEFUN(AC_TEST_FILE, [
ac_safe=`echo "$1" | sed 'y%./+-%__p_%'`
AC_MSG_CHECKING([for $1])
AC_CACHE_VAL(ac_cv_file_$ac_safe, [
if test -r $1; then
    eval "ac_cv_file_$ac_safe=yes"
else
    eval "ac_cv_file_$ac_safe=no"
fi
])dnl
if eval "test \"`echo '$ac_cv_file_'$ac_safe`\" = yes"; then
    AC_MSG_RESULT(yes)
    ifelse([$2], , :, [$2])
else
    AC_MSG_RESULT(no)
    ifelse([$3], , :, [$3])
fi
])

dnl ##
dnl ##  Check for socket/network size type
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_SOCKLENTYPE(<action-with-${ac_type}>)
dnl ##

dnl #   Background:
dnl #   this exists because of shortsightedness on the POSIX committee.
dnl #   BSD systems used "int *" as the parameter to accept(2),
dnl #   getsockname(2), getpeername(2) et al. Consequently many Unix
dnl #   flavors took an "int *" for that parameter. The POSIX committee
dnl #   decided that "int" was just too generic and had to be replaced
dnl #   with "size_t" almost everywhere. There's no problem with that
dnl #   when you're passing by value. But when you're passing by
dnl #   reference (as it is the case for accept(2) and friends) this
dnl #   creates a gross source incompatibility with existing programs.
dnl #   On 32-bit architectures it creates only a warning. On 64-bit
dnl #   architectures it creates broken code -- because "int *" is a
dnl #   pointer to a 64-bit quantity and "size_t *" is usually a pointer
dnl #   to a 32-bit quantity. Some Unix flavors adopted "size_t *" for
dnl #   the sake of POSIX compliance. Others ignored it because it was
dnl #   such a broken interface. Chaos ensued. POSIX finally woke up
dnl #   and decided that it was wrong and created a new type socklen_t.
dnl #   The only useful value for socklen_t is "int", and that's how
dnl #   everyone who has a clue implements it. It is almost always the
dnl #   case that this type should be defined to be an "int", unless the
dnl #   system being compiled for was created in the window of POSIX
dnl #   madness.

AC_DEFUN(AC_CHECK_SOCKLENTYPE,[dnl
AC_CHECK_TYPEDEF(socklen_t, sys/socket.h)
AC_CHECK_ARGTYPE(sys/types.h sys/socket.h, accept, 3, 3, [:])
AC_MSG_CHECKING(for fallback socklen_t)
AC_CACHE_VAL(ac_cv_check_socklentype, [
if test ".$ac_cv_typedef_socklen_t" = .yes; then
    ac_cv_check_socklentype='socklen_t'
elif test ".$ac_type" != .; then
    ac_cv_check_socklentype=`echo "$ac_type" | sed -e 's/[ 	]*\*$//'`
else
    ac_cv_check_socklentype='int'
fi
])
AC_MSG_RESULT([$ac_cv_check_socklentype])
ac_type="$ac_cv_check_socklentype"
ifelse([$1], , :, [$1])
])

dnl ##
dnl ##  Check for filedescriptor number type
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_NFDSTYPE(<action-with-${ac_type}>)
dnl ##

AC_DEFUN(AC_CHECK_NFDSTYPE,[dnl
AC_CHECK_TYPEDEF(nfds_t, poll.h)
AC_CHECK_ARGTYPE(sys/types.h poll.h, poll, 2, 3, [:])
AC_MSG_CHECKING(for fallback nfds_t)
AC_CACHE_VAL(ac_cv_check_nfdstype, [
if test ".$ac_cv_typedef_nfds_t" = .yes; then
    ac_cv_check_nfdstype='nfds_t'
elif test ".$ac_type" != .; then
    ac_cv_check_nfdstype=`echo "$ac_type" | sed -e 's/[ 	]*\*$//'`
else
    ac_cv_check_nfdstype='unsigned int'
fi
])
AC_MSG_RESULT([$ac_cv_check_nfdstype])
ac_type="$ac_cv_check_nfdstype"
ifelse([$1], , :, [$1])
])

dnl ##
dnl ##  Check for need of stackguard
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_NEED_FOR_SEPARATE_STACK(<define>)
dnl ##  acconfig.h:
dnl ##    #undef <define>
dnl ##  source.c:
dnl ##    #include "config.h"
dnl ##    #if <define> > 0
dnl ##        ...separate stack needed...
dnl ##    #else
dnl ##        ...separate stack not needed...
dnl ##    #endif
dnl ##

AC_DEFUN(AC_CHECK_NEED_FOR_SEPARATE_STACK,[dnl
AC_MSG_CHECKING(for need for separate stack)
AC_CACHE_VAL(ac_cv_need_for_separate_stack, [
case $HOSTTYPE in
ia64 )
    ac_cv_need_for_separate_stack=yes
    ;;
* )
    ac_cv_need_for_separate_stack=no
    ;;
esac
])dnl
AC_MSG_RESULT([$ac_cv_need_for_separate_stack])
if test ".$ac_cv_need_for_separate_stack" = .yes; then
    val="1"
else
    val="0"
fi
AC_DEFINE_UNQUOTED($1, $val)
])

dnl ##
dnl ##  Check for direction of stack growth
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_STACKGROWTH(<define>)
dnl ##  acconfig.h:
dnl ##    #undef <define>
dnl ##  source.c:
dnl ##    #include "config.h"
dnl ##    #if <define> < 0
dnl ##        ...stack grow down...
dnl ##    #else
dnl ##        ...stack grow up...
dnl ##    #endif
dnl ##

AC_DEFUN(AC_CHECK_STACKGROWTH,[dnl
AC_MSG_CHECKING(for direction of stack growth)
AC_CACHE_VAL(ac_cv_check_stackgrowth, [
cross_compile=no
AC_TRY_RUN(
changequote(<<, >>)dnl
<<
#include <stdio.h>
#include <stdlib.h>
static int iterate = 10;
static int growsdown(int *x)
{
    auto int y;
    y = (x > &y);
    if (--iterate > 0)
        y = growsdown(&y);
    if (y != (x > &y))
        exit(1);
    return y;
}
int main(int argc, char *argv[])
{
    FILE *f;
    auto int x;
    if ((f = fopen("conftestval", "w")) == NULL)
        exit(1);
    fprintf(f, "%s\n", growsdown(&x) ? "down" : "up");;
    fclose(f);
    exit(0);
}
>>
changequote([, ])dnl
,
ac_cv_check_stackgrowth=`cat conftestval`,
ac_cv_check_stackgrowth=down,
ac_cv_check_stackgrowth=down
)dnl
])dnl
AC_MSG_RESULT([$ac_cv_check_stackgrowth])
if test ".$ac_cv_check_stackgrowth" = ".down"; then
    val="-1"
else
    val="+1"
fi
AC_DEFINE_UNQUOTED($1, $val)
])

dnl ##
dnl ##  Check whether and how a POSIX compliant sigsetjmp(3) can be achieved
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_SJLJ(<success-action>, <failure-action>, <type-var>)
dnl ##

AC_DEFUN(AC_CHECK_SJLJ,[
AC_MSG_CHECKING(for signal-mask aware setjmp(3)/longjmp(3))
AC_CACHE_VAL(ac_cv_check_sjlj, [
AC_IFALLYES(func:setjmp func:longjmp, ac_cv_check_sjlj=sjlje, ac_cv_check_sjlj=none)
cross_compile=no
for testtype in ssjlj sjlj usjlj; do
OCFLAGS="$CFLAGS"
CFLAGS="$CFLAGS -DTEST_${testtype}"
AC_TRY_RUN(
changequote(<<, >>)dnl
<<
#if defined(TEST_ssjlj)
#define __JMP_BUF          sigjmp_buf
#define __SETJMP(buf)      sigsetjmp(buf,1)
#define __LONGJMP(buf,val) siglongjmp(buf,val)
#elif defined(TEST_sjlj)
#define __JMP_BUF          jmp_buf
#define __SETJMP(buf)      setjmp(buf)
#define __LONGJMP(buf,val) longjmp(buf,val)
#elif defined(TEST_usjlj)
#define __JMP_BUF          jmp_buf
#define __SETJMP(buf)      _setjmp(buf)
#define __LONGJMP(buf,val) _longjmp(buf,val)
#endif

#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>

static __JMP_BUF jb;

static void sighandler(int sig)
{
    sigset_t sigs;

    /* get signal mask */
    sigprocmask(SIG_SETMASK, NULL, &sigs);

    /* make sure USR1 is still blocked */
    if (!sigismember(&sigs, SIGUSR1))
        exit(1);

    /* block USR2 for us */
    sigaddset(&sigs, SIGUSR2);
    sigprocmask(SIG_SETMASK, &sigs, NULL);

    /* jump back to main */
    __LONGJMP(jb, 1);
    exit(1);
}

int main(int argc, char *argv[])
{
    FILE *fp;
    sigset_t sigs;
    struct sigaction sa;

    /* the default is that it fails */
    if ((fp = fopen("conftestval", "w")) == NULL)
        exit(1);
    fprintf(fp, "failed\n");
    fclose(fp);

    /* block USR1 and unblock USR2 signal */
    sigprocmask(SIG_SETMASK, NULL, &sigs);
    sigaddset(&sigs, SIGUSR1);
    sigdelset(&sigs, SIGUSR2);
    sigprocmask(SIG_SETMASK, &sigs, NULL);

    /* set jump buffer */
    if (__SETJMP(jb) == 0) {

        /* install signal handler for USR1 */
        memset((void *)&sa, 0, sizeof(struct sigaction));
        sigemptyset(&sa.sa_mask);
        sa.sa_handler = sighandler;
        sa.sa_flags = 0;
        sigaction(SIGUSR1, &sa, NULL);

        /* send USR1 signal (which is still blocked) */
        kill(getpid(), SIGUSR1);

        /* unblock USR1 and wait for it */
        sigprocmask(SIG_SETMASK, NULL, &sigs);
        sigdelset(&sigs, SIGUSR1);
        sigsuspend(&sigs);
        exit(1);
    }

    /* get signal mask again */
    sigprocmask(SIG_SETMASK, NULL, &sigs);

    /* make sure USR2 is again unblocked */
    if (sigismember(&sigs, SIGUSR2))
        exit(1);

    /* Fine... */
    if ((fp = fopen("conftestval", "w")) == NULL)
        exit(1);
    fprintf(fp, "ok\n");
    fclose(fp);
    exit(0);
}
>>
changequote([, ]),
rc=`cat conftestval`,
rc=failed,
rc=failed
)
CFLAGS="$OCFLAGS"
if test ".$rc" = .ok; then
    ac_cv_check_sjlj=$testtype
    break
fi
done
case $PLATFORM in
    *-*-linux* )
        braindead=no
        case "x`uname -r`" in
changequote(, )dnl
            x2.[23456789]* ) ;;
changequote([, ])
            * ) braindead=yes ;;
        esac
        case `grep __GLIBC_MINOR /usr/include/features.h | grep '#define' |\
              awk '{ printf("%s", [$]3 >= 1 ? "yes" : "no"); }'` in
            yes ) ;;
            * ) braindead=yes ;;
        esac
        case $braindead in
            yes ) ac_cv_check_sjlj=sjljlx ;;
            no  ) ac_cv_check_sjlj=ssjlj  ;;
        esac
        ;;
    *-*-isc* )
        ac_cv_check_sjlj=sjljisc
        ;;
    *-*-interix )
        #   Interix is a POSIX sub-system on Windows-NT which
        #   can use the Interactive UNIX dispatching code.
        ac_cv_check_sjlj=sjljisc
        ;;
    *-*-cygwin* )
        ac_cv_check_sjlj=sjljw32
        ;;
esac
])dnl
$3="$ac_cv_check_sjlj"
if test ".$ac_cv_check_sjlj" != .none; then
    AC_MSG_RESULT([yes: $ac_cv_check_sjlj])
    ifelse([$1], , :, [$1])
else
    AC_MSG_RESULT([no])
    ifelse([$2], , :, [$2])
fi
])dnl

dnl ##
dnl ##  Check for number of signals (NSIG)
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_NSIG(<define>)
dnl ##  acconfig.h:
dnl ##    #undef <define>
dnl ##  source.c:
dnl ##    #include "config.h"
dnl ##    ...<define>...

AC_DEFUN(AC_CHECK_NSIG,[dnl
AC_MSG_CHECKING(for number of signals)
cross_compile=no
AC_TRY_RUN(
changequote(<<, >>)dnl
<<
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>

int main(int argc, char *argv[])
{
    FILE *fp;
    int nsig;

#if defined(NSIG)
    nsig = NSIG;
#elif defined(_NSIG)
    nsig = _NSIG;
#else
    nsig = (sizeof(sigset_t)*8);
    if (nsig < 32)
        nsig = 32;
#endif
    if ((fp = fopen("conftestval", "w")) == NULL)
        exit(1);
    fprintf(fp, "%d\n", nsig);
    fclose(fp);
    exit(0);
}
>>
changequote([, ])dnl
,
nsig=`cat conftestval`,
nsig=32,
nsig=32
)dnl
AC_MSG_RESULT([$nsig])
AC_DEFINE_UNQUOTED($1, $nsig)
])

dnl ##
dnl ##  Check for an external/extension library.
dnl ##  - is aware of <libname>-config style scripts
dnl ##  - searches under standard paths include, lib, etc.
dnl ##  - searches under subareas like .libs, etc.
dnl ##
dnl ##  configure.in:
dnl ##      AC_CHECK_EXTLIB(<realname>, <libname>, <func>, <header>,
dnl ##                      [<success-action> [, <fail-action>]])
dnl ##  Makefile.in:
dnl ##      CFLAGS  = @CFLAGS@
dnl ##      LDFLAGS = @LDFLAGS@
dnl ##      LIBS    = @LIBS@
dnl ##  shell:
dnl ##      $ ./configure --with-<libname>[=DIR]
dnl ##

AC_DEFUN(AC_CHECK_EXTLIB,[dnl
AC_ARG_WITH($2,dnl
[  --with-]substr([$2[[=DIR]]                 ], 0, 19)[build against $1 library (default=no)],
    if test ".$with_$2" = .yes; then
        #   via config script
        $2_version=`($2-config --version) 2>/dev/null`
        if test ".$$2_version" != .; then
            CPPFLAGS="$CPPFLAGS `$2-config --cflags`"
            CFLAGS="$CFLAGS `$2-config --cflags`"
            LDFLAGS="$LDFLAGS `$2-config --ldflags`"
        fi
    else
        if test -d "$with_$2"; then
            found=0
            #   via config script
            for dir in $with_$2/bin $with_$2; do
                if test -f "$dir/$2-config"; then
                    $2_version=`($dir/$2-config --version) 2>/dev/null`
                    if test ".$$2_version" != .; then
                        CPPFLAGS="$CPPFLAGS `$dir/$2-config --cflags`"
                        CFLAGS="$CFLAGS `$dir/$2-config --cflags`"
                        LDFLAGS="$LDFLAGS `$dir/$2-config --ldflags`"
                        found=1
                        break
                    fi
                fi
            done
            #   via standard paths
            if test ".$found" = .0; then
                for dir in $with_$2/include/$2 $with_$2/include $with_$2; do
                    if test -f "$dir/$4"; then
                        CPPFLAGS="$CPPFLAGS -I$dir"
                        CFLAGS="$CFLAGS -I$dir"
                        found=1
                        break
                    fi
                done
                for dir in $with_$2/lib/$2 $with_$2/lib $with_$2; do
                    if test -f "$dir/lib$2.a" -o -f "$dir/lib$2.so"; then
                        LDFLAGS="$LDFLAGS -L$dir"
                        found=1
                        break
                    fi
                done
            fi
            #   in any subarea
            if test ".$found" = .0; then
changequote(, )dnl
                for file in x `find $with_$2 -name "$4" -type f -print`; do
                    test .$file = .x && continue
                    dir=`echo $file | sed -e 's;[[^/]]*$;;' -e 's;\(.\)/$;\1;'`
                    CPPFLAGS="$CPPFLAGS -I$dir"
                    CFLAGS="$CFLAGS -I$dir"
                done
                for file in x `find $with_$2 -name "lib$2.[[aso]]" -type f -print`; do
                    test .$file = .x && continue
                    dir=`echo $file | sed -e 's;[[^/]]*$;;' -e 's;\(.\)/$;\1;'`
                    LDFLAGS="$LDFLAGS -L$dir"
                done
changequote([, ])dnl
            fi
        fi
    fi
    AC_HAVE_HEADERS($4)
    AC_CHECK_LIB($2, $3)
    AC_IFALLYES(header:$4 lib:$2_$3, with_$2=yes, with_$2=no)
    if test ".$with_$2" = .no; then
        AC_ERROR([Unable to find $1 library])
    fi
,
if test ".$with_$2" = .; then
    with_$2=no
fi
)dnl
AC_MSG_CHECKING(whether to build against $1 library)
if test ".$with_$2" = .yes; then
    ifelse([$5], , :, [$5])
else
    ifelse([$6], , :, [$6])
fi
AC_MSG_RESULT([$with_$2])
])dnl

dnl ##
dnl ##  Check whether SVR4/SUSv2 makecontext(2), swapcontext(2) and
dnl ##  friends can be used for user-space context switching
dnl ##
dnl ##  configure.in:
dnl ##     AC_CHECK_MCSC(<success-action>, <failure-action>)
dnl ##

AC_DEFUN(AC_CHECK_MCSC, [
AC_MSG_CHECKING(for usable SVR4/SUSv2 makecontext(2)/swapcontext(2))
AC_CACHE_VAL(ac_cv_check_mcsc, [
AC_TRY_RUN([

#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

ucontext_t uc_child;
ucontext_t uc_main;

void child(void *arg)
{
    if (arg != (void *)12345)
        exit(1);
    if (swapcontext(&uc_child, &uc_main) != 0)
        exit(1);
}

int main(int argc, char *argv[])
{
    FILE *fp;
    void *stack;

    /* the default is that it fails */
    if ((fp = fopen("conftestval", "w")) == NULL)
        exit(1);
    fprintf(fp, "no\n");
    fclose(fp);

    /* configure a child user-space context */
    if ((stack = malloc(64*1024)) == NULL)
        exit(1);
    if (getcontext(&uc_child) != 0)
        exit(1);
    uc_child.uc_link = NULL;
    uc_child.uc_stack.ss_sp = (char *)stack+(32*1024);
    uc_child.uc_stack.ss_size = 32*1024;
    uc_child.uc_stack.ss_flags = 0;
    makecontext(&uc_child, child, 2, (void *)12345);

    /* switch into the user context */
    if (swapcontext(&uc_main, &uc_child) != 0)
        exit(1);

    /* Fine, child came home */
    if ((fp = fopen("conftestval", "w")) == NULL)
        exit(1);
    fprintf(fp, "yes\n");
    fclose(fp);

    /* die successfully */
    exit(0);
}
],
ac_cv_check_mcsc=`cat conftestval`,
ac_cv_check_mcsc=no,
ac_cv_check_mcsc=no
)dnl
])dnl
AC_MSG_RESULT([$ac_cv_check_mcsc])
if test ".$ac_cv_check_mcsc" = .yes; then
    ifelse([$1], , :, [$1])
else
    ifelse([$2], , :, [$2])
fi
])dnl

dnl ##
dnl ##  Check how stacks have to be setup for the functions
dnl ##  sigstack(2), sigaltstack(2) and makecontext(2).
dnl ##
dnl ##  configure.in:
dnl ##    AC_CHECK_STACKSETUP(sigstack|sigaltstack|makecontext, <macro-addr>, <macro-size>)
dnl ##  acconfig.h:
dnl ##    #undef HAVE_{SIGSTACK|SIGALTSTACK|MAKECONTEXT}
dnl ##    #undef HAVE_STACK_T
dnl ##  header.h.in:
dnl ##    @<macro-addr>@
dnl ##    @<macro-size>@
dnl ##  source.c:
dnl ##    #include "header.h"
dnl ##    xxx.sp_ss   = <macro-addr>(skaddr, sksize);
dnl ##    xxx.sp_size = <macro-size>(skaddr, sksize);
dnl ##

AC_DEFUN(AC_CHECK_STACKSETUP,[dnl
dnl #   check for consistent usage
ifelse($1,[sigstack],,[
ifelse($1,[sigaltstack],,[
ifelse($1,[makecontext],,[
errprint(__file__:__line__: [AC_CHECK_STACKSETUP: only sigstack, sigaltstack and makecontext supported
])])])])
dnl #   we require the C compiler and the standard headers
AC_REQUIRE([AC_HEADER_STDC])dnl
dnl #   we at least require the function to check
AC_CHECK_FUNCTIONS($1)
dnl #   sigaltstack on some platforms uses stack_t instead of struct sigaltstack
ifelse($1, sigaltstack, [
    AC_ONCE(stacksetup_stack_t, [
        AC_CHECK_TYPEDEF(stack_t, signal.h)
    ])
])
dnl #   display processing header
AC_MSG_CHECKING(for stack setup via $1)
dnl #   but cache the whole results
AC_CACHE_VAL(ac_cv_stacksetup_$1,[
if test ".$ac_cv_func_$1" = .no; then
    dnl #   no need to check anything when function is already missing
    ac_cv_stacksetup_$1="N.A.:/*N.A.*/,/*N.A.*/"
else
    dnl #   setup compile environment
    OCFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS -DTEST_$1"
    cross_compile=no
    dnl #   compile and run the test program
    AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(TEST_sigstack) || defined(TEST_sigaltstack)
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#endif
#if defined(TEST_makecontext)
#include <ucontext.h>
#endif
union alltypes {
    long   l;
    double d;
    void  *vp;
    void (*fp)(void);
    char  *cp;
};
static volatile char *handler_addr = (char *)0xDEAD;
#if defined(TEST_sigstack) || defined(TEST_sigaltstack)
static volatile int handler_done = 0;
void handler(int sig)
{
    char garbage[1024];
    int i;
    auto int dummy;
    for (i = 0; i < 1024; i++)
        garbage[i] = 'X';
    handler_addr = (char *)&dummy;
    handler_done = 1;
    return;
}
#endif
#if defined(TEST_makecontext)
static ucontext_t uc_handler;
static ucontext_t uc_main;
void handler(void)
{
    char garbage[1024];
    int i;
    auto int dummy;
    for (i = 0; i < 1024; i++)
        garbage[i] = 'X';
    handler_addr = (char *)&dummy;
    swapcontext(&uc_handler, &uc_main);
    return;
}
#endif
int main(int argc, char *argv[])
{
    FILE *f;
    char *skaddr;
    char *skbuf;
    int sksize;
    char result[1024];
    int i;
    sksize = 32768;
    skbuf = (char *)malloc(sksize*2+2*sizeof(union alltypes));
    if (skbuf == NULL)
        exit(1);
    for (i = 0; i < sksize*2+2*sizeof(union alltypes); i++)
        skbuf[i] = 'A';
    skaddr = skbuf+sizeof(union alltypes);
#if defined(TEST_sigstack) || defined(TEST_sigaltstack)
    {
        struct sigaction sa;
#if defined(TEST_sigstack)
        struct sigstack ss;
#elif defined(TEST_sigaltstack) && defined(HAVE_STACK_T)
        stack_t ss;
#else
        struct sigaltstack ss;
#endif
#if defined(TEST_sigstack)
        ss.ss_sp      = (void *)(skaddr + sksize);
        ss.ss_onstack = 0;
        if (sigstack(&ss, NULL) < 0)
            exit(1);
#elif defined(TEST_sigaltstack)
        ss.ss_sp    = (void *)(skaddr + sksize);
        ss.ss_size  = sksize;
        ss.ss_flags = 0;
        if (sigaltstack(&ss, NULL) < 0)
            exit(1);
#endif
        memset((void *)&sa, 0, sizeof(struct sigaction));
        sa.sa_handler = handler;
        sa.sa_flags = SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGUSR1, &sa, NULL);
        kill(getpid(), SIGUSR1);
        while (!handler_done)
            /*nop*/;
    }
#endif
#if defined(TEST_makecontext)
    {
        if (getcontext(&uc_handler) != 0)
            exit(1);
        uc_handler.uc_link = NULL;
        uc_handler.uc_stack.ss_sp    = (void *)(skaddr + sksize);
        uc_handler.uc_stack.ss_size  = sksize;
        uc_handler.uc_stack.ss_flags = 0;
        makecontext(&uc_handler, handler, 1);
        swapcontext(&uc_main, &uc_handler);
    }
#endif
    if (handler_addr == (char *)0xDEAD)
        exit(1);
    if (handler_addr < skaddr+sksize) {
        /* stack was placed into lower area */
        if (*(skaddr+sksize) != 'A')
             sprintf(result, "(skaddr)+(sksize)-%d,(sksize)-%d",
                     sizeof(union alltypes), sizeof(union alltypes));
        else
             strcpy(result, "(skaddr)+(sksize),(sksize)");
    }
    else {
        /* stack was placed into higher area */
        if (*(skaddr+sksize*2) != 'A')
            sprintf(result, "(skaddr),(sksize)-%d", sizeof(union alltypes));
        else
            strcpy(result, "(skaddr),(sksize)");
    }
    if ((f = fopen("conftestval", "w")) == NULL)
        exit(1);
    fprintf(f, "%s\n", result);
    fclose(f);
    exit(0);
}
],[
dnl #   test successully passed
ac_cv_stacksetup_$1=`cat conftestval`
ac_cv_stacksetup_$1="ok:$ac_cv_stacksetup_$1"
],[
dnl #   test failed
ac_cv_stacksetup_$1='guessed:(skaddr),(sksize)'
],[
dnl #   cross-platform => failed
ac_cv_stacksetup_$1='guessed:(skaddr),(sksize)'
])
dnl #   restore original compile environment
CFLAGS="$OCFLAGS"
])dnl
fi
dnl #   extract result ingredients of single cached result value
type=`echo $ac_cv_stacksetup_$1 | sed -e 's;:.*$;;'`
addr=`echo $ac_cv_stacksetup_$1 | sed -e 's;^.*:;;' -e 's;,.*$;;'`
size=`echo $ac_cv_stacksetup_$1 | sed -e 's;^.*:;;' -e 's;^.*,;;'`
dnl #   export result ingredients
$2="#define $2(skaddr,sksize) ($addr)"
$3="#define $3(skaddr,sksize) ($size)"
AC_SUBST($2)dnl
AC_SUBST($3)dnl
dnl #   display result indicator
AC_MSG_RESULT([$type])
dnl #   display results in detail
AC_MSG_VERBOSE([$]$2)
AC_MSG_VERBOSE([$]$3)
])

