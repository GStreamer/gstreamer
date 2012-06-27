#!/bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PROJECT=gstreamer-vaapi
TEST_TYPE=-d
FILE=gst-libs

test $TEST_TYPE $FILE || {
    echo "You must run this script in the top-level $PROJECT directory"
    exit 1
}

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

if test -z "$NO_CONFIGURE"; then
    ./configure "$@" && echo "Now type 'make' to compile $PROJECT."
fi
