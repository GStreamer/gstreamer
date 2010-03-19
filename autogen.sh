#! /bin/sh

GTKDOCIZE=`which gtkdocize`
if test -z $GTKDOCIZE; then
    echo "*** No gtk-doc support ***"
    echo "EXTRA_DIST =" > gtk-doc.make
else
    gtkdocize || exit $?
    # we need to patch gtk-doc.make to support pretty output with
    # libtool 1.x.  Should be fixed in the next version of gtk-doc.
    # To be more resilient with the various versions of gtk-doc one
    # can find, just sed gkt-doc.make rather than patch it.
    sed -e 's#) --mode=compile#) --tag=CC --mode=compile#' gtk-doc.make > gtk-doc.temp \
        && mv gtk-doc.temp gtk-doc.make
    sed -e 's#) --mode=link#) --tag=CC --mode=link#' gtk-doc.make > gtk-doc.temp \
        && mv gtk-doc.temp gtk-doc.make
fi

autoreconf -v --install
./configure "$@"
