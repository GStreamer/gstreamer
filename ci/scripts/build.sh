#! /bin/bash

set -eux

# Expects:
# BUILD_TYPE: Proxy of meson's --default-library arg
#   must be 'shared' or 'static' or 'both'
# BUILD_GST_DEBUG: Build with gst debug symbols or not
#   must be a string like this: -Dgstreamer:gst_debug=true.
# GST_WERROR: make warning fatal or not
#   must be a string of a boolean, "true" or "false". Not yaml bool.
# SUBPROJECTS_CACHE_DIR: The location in the image of the subprojects cache

export RUSTUP_HOME="/usr/local/rustup"
export CARGO_HOME="/usr/local/cargo"
export PATH="/usr/local/cargo/bin:$PATH"

date -R
ci/scripts/handle-subprojects-cache.py --cache-dir /subprojects subprojects/

ARGS="${BUILD_TYPE:---default-library=both} ${BUILD_GST_DEBUG:--Dgstreamer:gst_debug=true} ${MESON_ARGS}"
echo "Werror: $GST_WERROR"

if [ "$GST_WERROR" = "true" ]; then
  ARGS="$ARGS --native-file ./ci/meson/gst-werror.ini"
fi

date -R
meson setup build/ --native-file ./ci/meson/gst-ci-cflags.ini  ${ARGS}
date -R

if command -v ccache
then
  ccache --show-stats
fi

date -R
meson compile -C build/
date -R

if command -v ccache
then
  ccache --show-stats
fi
