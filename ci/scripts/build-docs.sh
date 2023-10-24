#! /bin/bash

set -eux

export PATH="/usr/local/cargo/bin/:/usr/local/bin/:$PATH"
export RUSTUP_HOME="/usr/local/rustup"
export CARGO_HOME="/usr/local/cargo"

./ci/scripts/handle-subprojects-cache.py --cache-dir /subprojects subprojects/

echo "$MESON_ARGS"
meson setup build/ $MESON_ARGS
ccache --show-stats

ninja -C build/ update_girs
# Ignore modifications to wrap files made by meson
git checkout $(git ls-files 'subprojects/*.wrap')
./ci/scripts/check-diff.py "gir files"

./gst-env.py ninja -C build/ plugins_doc_caches

# Ignore modifications to wrap files made by meson
git checkout $(git ls-files 'subprojects/*.wrap')
./ci/scripts/check-diff.py

export GI_TYPELIB_PATH="$PWD/girs"
hotdoc run --conf-file build/subprojects/gst-docs/GStreamer-doc.json

mv build/subprojects/gst-docs/GStreamer-doc/html documentation/
