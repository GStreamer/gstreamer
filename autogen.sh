#!/bin/bash
# Run this to generate all the initial makefiles, etc.

DIE=0
package=gstreamer-plugins
srcfile=sys/oss/Makefile.am
#DEBUG=defined

for i in $@; do
    if test "$i" = "--autogen-noconfigure"; then
        NOCONFIGURE=defined
        echo "+ configure run disabled"
    elif test "$i" = "--autogen-nocheck"; then
        NOCHECK=defined
        echo "+ autotools version check disabled"
    elif test "$i" = "--autogen-debug"; then
        DEBUG=defined
        echo "+ debug output enabled"
    elif test "$i" = "--help"; then
        echo "autogen.sh help options: "
        echo " --autogen-noconfigure    don't run the configure script"
        echo " --autogen-nocheck        don't do version checks"
        echo " --autogen-debug          debug the autogen process"
        echo "continuing with the autogen in order to get configure help messages..."
    fi
done

debug ()
# print out a debug message if DEBUG is a defined variable
{
  if test ! -z "$DEBUG"
  then
    echo "DEBUG: $1"
  fi
}

version_check ()
# check the version of a package
# first argument : package name (executable)
# second argument : source download url
# rest of arguments : major, minor, micro version
{
  PACKAGE=$1
  URL=$2
  MAJOR=$3
  MINOR=$4
  MICRO=$5

  WRONG=

  debug "major $MAJOR minor $MINOR micro $MICRO"
  VERSION=$MAJOR
  if test ! -z "$MINOR"; then VERSION=$VERSION.$MINOR; else MINOR=0; fi
  if test ! -z "$MICRO"; then VERSION=$VERSION.$MICRO; else MICRO=0; fi

  debug "major $MAJOR minor $MINOR micro $MICRO"
  
  test -z "$NOCHECK" && {
      echo -n "+ checking for $1 >= $VERSION ... "
  } || {
      return 0
  }
  
  ($PACKAGE --version) < /dev/null > /dev/null 2>&1 || 
  {
	echo
	echo "You must have $PACKAGE installed to compile $package."
	echo "Download the appropriate package for your distribution,"
	echo "or get the source tarball at $URL"
	return 1
  }
  # the following line is carefully crafted sed magic
  pkg_version=`$PACKAGE --version|head -n 1|sed 's/^[a-zA-z\.\ ()]*//;s/ .*$//'`
  debug "pkg_version $pkg_version"
  pkg_major=`echo $pkg_version | cut -d. -f1`
  pkg_minor=`echo $pkg_version | cut -d. -f2`
  pkg_micro=`echo $pkg_version | cut -d. -f3`
  test -z "$pkg_minor" && pkg_minor=0
  test -z "$pkg_micro" && pkg_micro=0

  debug "found major $pkg_major minor $pkg_minor micro $pkg_micro"

  #start checking the version
  debug "version check"

  if [ ! "$pkg_major" \> "$MAJOR" ]; then
    debug "$pkg_major <= $MAJOR"
    if [ "$pkg_major" \< "$MAJOR" ]; then
      WRONG=1
    elif [ ! "$pkg_minor" \> "$MINOR" ]; then
      if [ "$pkg_minor" \< "$MINOR" ]; then
        WRONG=1
      elif [ "$pkg_micro" \< "$MICRO" ]; then
	WRONG=1
      fi
    fi
  fi

  if test ! -z "$WRONG"; then
    echo "found $pkg_version, not ok !"
    echo
    echo "You must have $PACKAGE $VERSION or greater to compile $package."
    echo "Get the latest version from $URL"
    return 1
  else
    echo "found $pkg_version, ok."
  fi
}

# autoconf 2.52d has a weird issue involving a yes:no error
# so don't allow it's use
ac_version=`autoconf --version|head -n 1|sed 's/^[a-zA-z\.\ ()]*//;s/ .*$//'`
if test "$ac_version" = "2.52d"; then
  echo "autoconf 2.52d has an issue with our current build."
  echo "We don't know who's to blame however.  So until we do, get a"
  echo "regular version.  RPM's of a working version are on the gstreamer site."
  exit 1
fi


version_check "autoconf" "ftp://ftp.gnu.org/pub/gnu/autoconf/" 2 52 || DIE=1
version_check "automake" "ftp://ftp.gnu.org/pub/gnu/automake/" 1 5 || DIE=1
version_check "libtool" "ftp://ftp.gnu.org/pub/gnu/libtool/" 1 4 0 || DIE=1
version_check "pkg-config" "http://www.freedesktop.org/software/pkgconfig" 0 8 0 || DIE=1

if test "$DIE" -eq 1; then
	exit 1
fi

test -f $srcfile || {
	echo "You must run this script in the top-level $package directory"
	exit 1
}

if test -z "$*"; then
	echo "I am going to run ./configure with no arguments - if you wish "
        echo "to pass any to it, please specify them on the $0 command line."
fi

echo "+ creating acinclude.m4"
cat m4/*.m4 > acinclude.m4

echo "+ running aclocal ..."
aclocal $ACLOCAL_FLAGS || {
	echo
	echo "aclocal failed - check that all needed development files are present on system"
	exit 1
}

# FIXME : why does libtoolize keep complaining about aclocal ?
## echo "+ running libtoolize ..."
## libtoolize --copy --force

echo "+ running autoheader ... "
autoheader || {
	echo
	echo "autoheader failed"
	exit 1
}
# touch the stamp-h.in build stamp so we don't re-run autoheader in maintainer mode -- wingo
echo timestamp > stamp-h.in 2> /dev/null
echo "+ running autoconf ... "
autoconf || {
	echo
	echo "autoconf failed"
	exit 1
}
echo "+ running automake ... "
automake -a -c || {
	echo
	echo "automake failed"
	exit 1
}

CONFIGURE_OPT='--enable-maintainer-mode --enable-plugin-builddir --enable-debug --enable-DEBUG'

# if enable exists, add an -enable option for each of the lines in that file
if test -f enable; then
  for a in `cat enable`; do
    CONFIGURE_OPT="$CONFIGURE_OPT --enable-$a"
  done
fi

# if disable exists, add an -disable option for each of the lines in that file
if test -f disable; then
  for a in `cat disable`; do
    CONFIGURE_OPT="$CONFIGURE_OPT --disable-$a"
  done
fi

test -n "$NOCONFIGURE" && {
    echo "skipping configure stage for package $package, as requested."
    echo "autogen.sh done."
    exit 0
}

echo "+ running configure ... "
echo "./configure default flags: $CONFIGURE_OPT"
echo "using: $CONFIGURE_OPT $@"
echo

./configure $CONFIGURE_OPT "$@" || {
	echo
	echo "configure failed"
	exit 1
}

echo 
echo "Now type 'make' to compile $package."
