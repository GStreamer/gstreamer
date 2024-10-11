#! /bin/bash

builddir="$1"

if [[ -z "$builddir" ]]; then
  echo "Usage: build-docs.sh <build_directory>"
  exit 1
fi

set -eux

export PATH="/usr/local/cargo/bin/:/usr/local/bin/:$PATH"
export RUSTUP_HOME="/usr/local/rustup"
export CARGO_HOME="/usr/local/cargo"

./ci/scripts/handle-subprojects-cache.py --cache-dir /subprojects subprojects/

echo "$MESON_ARGS"
meson setup "$builddir" $MESON_ARGS
ccache --show-stats

ninja -C "$builddir" update_girs
# Ignore modifications to wrap files made by meson
git checkout $(git ls-files 'subprojects/*.wrap')
./ci/scripts/check-diff.py "gir files"

./gst-env.py --builddir "$builddir" ninja -C "$builddir" plugins_doc_caches

# Ignore modifications to wrap files made by meson
git checkout $(git ls-files 'subprojects/*.wrap')
./ci/scripts/check-diff.py

export GI_TYPELIB_PATH="$PWD/girs"
hotdoc run --conf-file "$builddir/subprojects/gst-docs/GStreamer-doc.json"

mv "$builddir/subprojects/gst-docs/GStreamer-doc/html" documentation/
