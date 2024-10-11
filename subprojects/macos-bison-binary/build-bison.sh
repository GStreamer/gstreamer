#!/bin/bash

set -e

if ! [[ -e meson.build ]] || ! grep -q "^project('macos-bison-binary'" meson.build; then
  echo "Could not find macos-bison-binary meson.build"
  exit 1
fi

VER="$(sed -n "s/project.*version.*'\(.*\)'.*/\1/p" meson.build)"
ARCH=$(uname -m)
[[ $ARCH = arm64 ]] && ARCH="aarch64"
SRCDIR="bison-$VER"
SRC="$SRCDIR.tar.gz"
OUTDIR="bison-$VER-macos-$ARCH"
OUT="bison-$VER-macos-$ARCH.tar.bz2"

if ! [[ -e $SRC ]]; then
  curl -O -L https://ftp.gnu.org/gnu/bison/$SRC
fi

TARGETDIR="$PWD"

rm -rf $SRCDIR
tar -xf $SRC && cd $SRCDIR
./configure --prefix="$TARGETDIR/_install/" --enable-relocatable
make -j8
make install

cd "$TARGETDIR"
rm -rf _install/share/{info,man,doc}
strip -u -r _install/bin/bison

rm -rf "$OUTDIR"
mv _install "$OUTDIR"
tar -cvf "$OUT" "$OUTDIR"/
CHECKSUM=$(shasum -a 256 "$OUT" | awk '{print $1}')
sed -I '' -e "s/  '$ARCH': '.*'/  '$ARCH': '$CHECKSUM'/g" meson.build
