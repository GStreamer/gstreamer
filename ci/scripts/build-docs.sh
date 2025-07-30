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

# Check GES children properties documentation is up to date
echo "Checking GES children properties documentation..."
# Force building python extension modules to ensure the _gi_gst python module is built
ninja -C "$builddir" gst-python@@gst-python-extensions
./gst-env.py --builddir "$builddir" python3 subprojects/gst-editing-services/docs/libs/document-children-props.py

# Check if there are any changes in the markdown files
if ! git diff --ignore-submodules --exit-code subprojects/gst-editing-services/docs/libs/*-children-props.md; then
  echo "ERROR: GES children properties documentation is out of date!"

  # Create diff for download
  diffsdir='diffs'
  mkdir -p "$diffsdir"
  diffname="$diffsdir/ges_children_properties_documentation.diff"
  git diff --ignore-submodules subprojects/gst-editing-services/docs/libs/*-children-props.md > "$diffname"

  echo ""
  echo "You can download and apply the changes with:"
  echo "     \$ curl -L \${CI_ARTIFACTS_URL}/$diffname | git apply -"
  echo ""
  echo "(note that it might take a few minutes for artifacts to be available on the server)"

  exit 1
fi
echo "GES children properties documentation is up to date"

pip3 install bs4
python3 subprojects/gst-docs/scripts/rust_doc_unifier.py documentation/
