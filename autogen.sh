#!/bin/sh
#
# gst-plugins-bad autogen.sh
#
# Run this to generate all the initial makefiles, etc.
#
# This file has been generated from common/autogen.sh.in via common/update-autogen


test -n "$srcdir" || srcdir=`dirname "$0"`
test -n "$srcdir" || srcdir=.

olddir=`pwd`
cd "$srcdir"

package=gst-plugins-bad
srcfile=gst-plugins-bad.doap

# Make sure we have common
if test ! -f common/gst-autogen.sh;
then
  echo "+ Setting up common submodule"
  git submodule init
fi
git submodule update

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
    if ! ln -s ../../common/hooks/pre-commit.hook .git/hooks/pre-commit 2> /dev/null
    then
        echo "Failed to create commit hook symlink, copying instead ..."
        cp common/hooks/pre-commit.hook .git/hooks/pre-commit
    fi
fi

# GNU gettext automake support doesn't get along with git.
# https://bugzilla.gnome.org/show_bug.cgi?id=661128
if test -d po ; then
  touch -t 200001010000 po/gst-plugins-bad-1.0.pot
fi

CONFIGURE_DEF_OPT='--enable-maintainer-mode --enable-gtk-doc'

if test "x$package" = "xgstreamer"; then
  CONFIGURE_DEF_OPT="$CONFIGURE_DEF_OPT --enable-failing-tests --enable-poisoning"
elif test "x$package" = "xgst-plugins-bad"; then
  CONFIGURE_DEF_OPT="$CONFIGURE_DEF_OPT --with-player-tests"
fi

autogen_options $@

printf "+ check for build tools"
if test -z "$NOCHECK"; then
  echo

  printf "  checking for autoreconf ... "
  echo
  which "autoreconf" 2>/dev/null || {
    echo "not found! Please install the autoconf package."
    exit 1
  }

  printf "  checking for pkg-config ... "
  echo
  which "pkg-config" 2>/dev/null || {
    echo "not found! Please install pkg-config."
    exit 1
  }
else
  echo ": skipped version checks"
fi

# if no arguments specified then this will be printed
if test -z "$*" && test -z "$NOCONFIGURE"; then
  echo "+ checking for autogen.sh options"
  echo "  This autogen script will automatically run ./configure as:"
  echo "  ./configure $CONFIGURE_DEF_OPT"
  echo "  To pass any additional options, please specify them on the $0"
  echo "  command line."
fi

toplevel_check $srcfile

# autopoint
if test -d po && grep ^AM_GNU_GETTEXT_VERSION configure.ac >/dev/null ; then
  tool_run "autopoint" "--force"
fi

# aclocal
if test -f acinclude.m4; then rm acinclude.m4; fi

autoreconf --force --install || exit 1

test -n "$NOCONFIGURE" && {
  echo "+ skipping configure stage for package $package, as requested."
  echo "+ autogen.sh done."
  exit 0
}

cd "$olddir"

echo "+ running configure ... "
test ! -z "$CONFIGURE_DEF_OPT" && echo "  default flags:  $CONFIGURE_DEF_OPT"
test ! -z "$CONFIGURE_EXT_OPT" && echo "  external flags: $CONFIGURE_EXT_OPT"
echo

echo "$srcdir/configure" $CONFIGURE_DEF_OPT $CONFIGURE_EXT_OPT
"$srcdir/configure" $CONFIGURE_DEF_OPT $CONFIGURE_EXT_OPT || {
        echo "  configure failed"
        exit 1
}

echo "Now type 'make' to compile $package."
