#! /bin/bash

builddir="$1"
if [[ -z "$builddir" ]]; then
  echo "Usage: build-docs.sh <build_directory>"
  exit 1
fi

set -eux

source "ci/scripts/source_image_env.sh"

# Special doc mode for V4L2 stateless codecs
export GST_V4L2_CODEC_GEN_DOC=1

meson_args="${MESON_ARGS:--Ddoc=enabled -Drs=enabled -Dgst-docs:fatal_warnings=true}"
echo "$meson_args"
meson setup "$builddir" $meson_args
ccache --show-stats

ninja -C "$builddir" update_girs
# Ignore modifications to wrap files made by meson
git checkout $(git ls-files 'subprojects/*.wrap')
./ci/scripts/check-diff.py "gir files"

./gst-env.py --builddir "$builddir" ninja -C "$builddir" plugins_doc_caches

# Ignore modifications to wrap files made by meson
git checkout $(git ls-files 'subprojects/*.wrap')
./ci/scripts/check-diff.py

ninja -C "$builddir" subprojects/gst-docs/sitemap.txt

export GI_TYPELIB_PATH=$PWD/girs
hotdoc run --conf-file "$builddir"/subprojects/gst-docs/GStreamer-doc.json

mv "$builddir/subprojects/gst-docs/GStreamer-doc/html" documentation/

pip3 install bs4
python3 subprojects/gst-docs/scripts/rust_doc_unifier.py documentation/
