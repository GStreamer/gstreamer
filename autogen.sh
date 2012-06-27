#!/bin/sh

PROJECT="gstreamer-vaapi"

test -n "$srcdir" || srcdir="`dirname \"$0\"`"
test -n "$srcdir" || srcdir=.

if ! test -f "$srcdir/configure.ac"; then
    echo "Failed to find the top-level $PROJECT directory"
    exit 1
fi

olddir="`pwd`"
cd "$srcdir"

mkdir -p m4

GTKDOCIZE=`which gtkdocize`
if test -z $GTKDOCIZE; then
    echo "*** No gtk-doc support ***"
    echo "EXTRA_DIST =" > gtk-doc.make
else
    gtkdocize || exit $?
fi

AUTORECONF=`which autoreconf`
if test -z $AUTORECONF; then
    echo "*** No autoreconf found ***"
    exit 1
else
    autoreconf -v --install || exit $?
fi

cd "$olddir"

if test -z "$NO_CONFIGURE"; then
    $srcdir/configure "$@" && echo "Now type 'make' to compile $PROJECT."
fi
