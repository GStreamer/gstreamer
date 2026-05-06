#!/bin/bash -eux

# build-oss-fuzz.sh
#
# Build script which is executed by oss-fuzz build.sh
#
# $SRC: location of code checkouts
# $OUT: location to put fuzzing targets and corpus
# $WORK: writable directory where all compilation should be executed
#
# /!\ Do not override any CC, CXX, CFLAGS, ... variables
#

rm -rf $WORK/*
rm -rf $OUT/lib $OUT/*_seed_corpus.zip

# Prefix where we will temporarily install everything
PREFIX=$WORK/prefix
mkdir -p $PREFIX
# always try getting the arguments for static compilation/linking
# Fixes GModule not being picked when gstreamer-1.0.pc is looked up by meson
# more or less https://github.com/mesonbuild/meson/pull/6629
export PKG_CONFIG="`which pkg-config` --static"
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig
export PATH=$PREFIX/bin:$PATH

# Minimize gst-debug level/code
export CFLAGS="$CFLAGS -DGST_LEVEL_MAX=2"

echo "CFLAGS : " $CFLAGS
echo "CXXFLAGS : " $CXXFLAGS

# Switch to work directory
cd $WORK

mkdir -p $OUT/lib/gstreamer-1.0

# Note: We don't use/build orc since it still seems to be problematic
# with clang and the various sanitizers.

# For now we only build core and base. Add other modules when/if needed
meson \
    --prefix=$PREFIX \
    --libdir=lib \
    --default-library=shared \
    --force-fallback-for=zlib,ogg,vorbis,theora \
    -Db_lundef=false \
    -Doss_fuzz=enabled \
    -Dglib:oss_fuzz=enabled \
    -Dglib:libmount=disabled \
    -Dglib:tests=false \
    -Ddoc=disabled \
    -Dexamples=disabled \
    -Dintrospection=disabled \
    -Dgood=enabled \
    -Dgst-plugins-good:auto_features=disabled \
    -Dgst-plugins-good:isomp4=enabled \
    -Dgst-plugins-good:matroska=enabled \
    -Dgst-plugins-good:flv=enabled \
    -Dgst-plugins-good:avi=enabled \
    -Dgst-plugins-good:audioparsers=enabled \
    -Dgst-plugins-good:wavparse=enabled \
    -Dgst-plugins-good:id3demux=enabled \
    -Dgst-plugins-good:flac=disabled \
    -Dbad=enabled \
    -Dgst-plugins-bad:auto_features=disabled \
    -Dgst-plugins-bad:mpegtsdemux=enabled \
    -Dgst-plugins-bad:videoparsers=enabled \
    -Dgst-plugins-bad:jpegformat=enabled \
    -Dugly=disabled \
    -Dlibav=disabled \
    -Dges=disabled \
    -Dsharp=disabled \
    -Drs=disabled \
    -Dpython=disabled \
    -Dlibnice=disabled \
    -Ddevtools=disabled \
    -Drtsp_server=disabled \
    -Dgst-examples=disabled \
    -Dqt5=disabled \
    -Dorc=disabled \
    -Dgstreamer:tracer_hooks=false \
    -Dgst-plugins-base:pango=disabled \
    _builddir \
    $SRC/gstreamer
ninja -C _builddir
ninja -C _builddir install

# copy out the fuzzing binaries
for BINARY in $(find _builddir/ci/fuzzing -type f -executable -print)
do
  BASENAME=${BINARY##*/}
  rm -rf "$OUT/$BASENAME*"
  cp $BINARY $OUT/$BASENAME
  patchelf --set-rpath '$ORIGIN/lib' $OUT/$BASENAME
done

# copy any relevant corpus
for CORPUS in $(find "$SRC/gstreamer/ci/fuzzing" -type f -name "*.corpus"); do
  BASENAME=${CORPUS##*/}
  pushd "$SRC/gstreamer"
  zip $OUT/${BASENAME%%.*}_seed_corpus.zip . -ws -r -i@$CORPUS
  popd
done

# copy dependant libraries
find "$PREFIX/lib" -maxdepth 1 -type f -name "*.so*" -exec cp -d "{}" $OUT/lib \; -print
# add rpath that point to the correct place to all shared libraries
find "$OUT/lib" -maxdepth 1 -type f -name "*.so*" -exec patchelf --debug --set-rpath '$ORIGIN' {} \;
find "$PREFIX/lib" -maxdepth 1 -type l -name "*.so*" -exec cp -d "{}" $OUT/lib \; -print

find "$PREFIX/lib/gstreamer-1.0" -maxdepth 1 -type f -name "*.so" -exec cp -d "{}" $OUT/lib/gstreamer-1.0 \;
find "$OUT/lib/gstreamer-1.0" -type f -name "*.so*" -exec patchelf --debug --set-rpath '$ORIGIN/..' {} \;

# make it easier to spot dependency issues
find "$OUT/lib/gstreamer-1.0" -maxdepth 1 -type f -name "*.so" -print -exec ldd {} \;
