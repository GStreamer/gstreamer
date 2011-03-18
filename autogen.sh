#!/bin/sh
# Run this to generate all the initial makefiles, etc.

DIE=0
package=gst-ffmpeg
srcfile=configure.ac
have_svn=`which svn`
# FFMPEG specific properties
. ./ffmpegrev

# make sure we have common
if test ! -f common/gst-autogen.sh; 
then 
  echo "+ Setting up common submodule"
  git submodule init
fi
git submodule update

if test -x $have_svn && [ $have_svn ];
then
    co_ffmpeg=no

    if test ! -f $FFMPEG_CO_DIR/configure; then
      co_ffmpeg=yes
    else
      if ! svn info gst-libs/ext/ffmpeg | grep "URL: $FFMPEG_SVN" > /dev/null; then
        echo "FFmpeg checkout is on the wrong branch. Re-fetching."
        co_ffmpeg=yes
      fi
    fi

    if [ "$co_ffmpeg" = "yes" ]; then
	# checkout ffmpeg from its repository
	rm -rf $FFMPEG_CO_DIR
	echo "+ getting ffmpeg from svn"
	svn -r $FFMPEG_REVISION co $FFMPEG_SVN $FFMPEG_CO_DIR
    else
	# update ffmpeg from its repository
	echo "+ updating ffmpeg checkout"
	svn -r $FFMPEG_REVISION up $FFMPEG_CO_DIR
    fi
    if [ "x$FFMPEG_EXTERNALS_REVISION" != "x" ]; then
	echo "+ updating externals"
	svn update -r $FFMPEG_EXTERNALS_REVISION $FFMPEG_CO_DIR/libswscale
    fi
else
    echo "Subversion needed for ffmpeg checkout, please install and/or add to \$PATH"
    exit 0
fi

# source helper functions
if test ! -f common/gst-autogen.sh;
then
  echo There is something wrong with your source tree.
  echo You are missing common/gst-autogen.sh
  exit 1
fi
. common/gst-autogen.sh

# install pre-commit hook for doing clean commits
if test ! \( -x .git/hooks/pre-commit -a -L .git/hooks/pre-commit \);
then
    rm -f .git/hooks/pre-commit
    ln -s ../../common/hooks/pre-commit.hook .git/hooks/pre-commit
fi

autogen_options $@

printf "+ check for build tools"
if test ! -z "$NOCHECK"; then echo " skipped"; else  echo; fi
version_check "autoconf" "$AUTOCONF autoconf autoconf270 autoconf269 autoconf268 autoconf267 autoconf266 autoconf265 autoconf264 autoconf263 autoconf262 autoconf261 autoconf260" \
              "ftp://ftp.gnu.org/pub/gnu/autoconf/" 2 60 || DIE=1
version_check "automake" "$AUTOMAKE automake automake-1.11 automake-1.10" \
              "ftp://ftp.gnu.org/pub/gnu/automake/" 1 10 || DIE=1
version_check "libtoolize" "$LIBTOOLIZE libtoolize glibtoolize" \
              "ftp://ftp.gnu.org/pub/gnu/libtool/" 1 5 0 || DIE=1
version_check "pkg-config" "" \
              "http://www.freedesktop.org/software/pkgconfig" 0 8 0 || DIE=1

die_check $DIE

aclocal_check || DIE=1
autoheader_check || DIE=1

die_check $DIE

# if no arguments specified then this will be printed
if test -z "$*"; then
  echo "+ checking for autogen.sh options"
  echo "  This autogen script will automatically run ./configure as:"
  echo "  ./configure $CONFIGURE_DEF_OPT"
  echo "  To pass any additional options, please specify them on the $0"
  echo "  command line."
fi

toplevel_check $srcfile

tool_run "$libtoolize" "--copy --force"
tool_run "$aclocal" "-I m4 -I common/m4 $ACLOCAL_FLAGS"
tool_run "$autoheader"

# touch the stamp-h.in build stamp so we don't re-run autoheader in maintainer mode
echo timestamp > stamp-h.in 2> /dev/null

tool_run "$autoconf"
tool_run "$automake" "-a -c -Wno-portability"

# if enable exists, add an -enable option for each of the lines in that file
if test -f enable; then
  for a in `cat enable`; do
    CONFIGURE_FILE_OPT="--enable-$a"
  done
fi

# if disable exists, add an -disable option for each of the lines in that file
if test -f disable; then
  for a in `cat disable`; do
    CONFIGURE_FILE_OPT="$CONFIGURE_FILE_OPT --disable-$a"
  done
fi

test -n "$NOCONFIGURE" && {
  echo "+ skipping configure stage for package $package, as requested."
  echo "+ autogen.sh done."
  exit 0
}

echo "+ running configure ... "
test ! -z "$CONFIGURE_DEF_OPT" && echo "  ./configure default flags: $CONFIGURE_DEF_OPT"
test ! -z "$CONFIGURE_EXT_OPT" && echo "  ./configure external flags: $CONFIGURE_EXT_OPT"
test ! -z "$CONFIGURE_FILE_OPT" && echo "  ./configure enable/disable flags: $CONFIGURE_FILE_OPT"
echo

./configure $CONFIGURE_DEF_OPT $CONFIGURE_EXT_OPT $CONFIGURE_FILE_OPT || {
        echo "  configure failed"
        exit 1
}

echo
echo "Now type 'make' to compile $package."
