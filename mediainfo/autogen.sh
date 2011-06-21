#!/bin/sh
# Run this to generate all the initial makefiles, etc.

# a silly hack that generates autoregen.sh but it's handy
echo "#!/bin/sh" > autoregen.sh
echo "./autogen.sh $@ \$@" >> autoregen.sh
chmod +x autoregen.sh

mkdir -p m4

test -n "$srcdir" || srcdir=$(dirname "$0")
test -n "$srcdir" || srcdir=.
(
  cd "$srcdir" &&
  AUTOPOINT='intltoolize --automake -c -f' autoreconf -fivm
) || exit
test -n "$NOCONFIGURE" || "$srcdir/configure" "$@"

