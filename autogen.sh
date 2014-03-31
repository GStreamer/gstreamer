#!/bin/sh
which autoreconf > /dev/null
if [ $? -ne 0 ] ; then
       echo "Please install autoconf" && exit 1;
fi
mkdir -p m4
autoreconf  -i --force --warnings=none -I . -I m4
./configure --enable-maintainer-mode $*
