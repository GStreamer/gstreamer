#! /bin/bash

builddir="$1"

error="${GST_WERROR:-false}"
gtk_args="${GTK_ARGS:-}"
meson_args="${MESON_ARGS:-}"

if [[ -z "$builddir" ]]; then
  echo "Usage: build.sh <build_directory>"
  exit 1
fi

set -eux

source "ci/scripts/source_image_env.sh"

# Expects:
# BUILD_TYPE: Proxy of meson's --default-library arg
#   must be 'shared' or 'static' or 'both'
# BUILD_GST_DEBUG: Build with gst debug symbols or not
#   must be a string like this: -Dgstreamer:gst_debug=true.
# GST_WERROR: make warning fatal or not
#   must be a string of a boolean, "true" or "false". Not yaml bool.
# SUBPROJECTS_CACHE_DIR: The location in the image of the subprojects cache

# nproc works on linux
# sysctl for macos
_jobs=$(nproc || sysctl -n hw.ncpu)
jobs="${FDO_CI_CONCURRENT:-$_jobs}"

date -R

ARGS="${BUILD_TYPE:---default-library=both} ${BUILD_GST_DEBUG:--Dgstreamer:gst_debug=true} $meson_args $gtk_args"
echo "Werror: $error"

# If the variable is not true, we are either running locally or explicitly false. Thus false by default.
if [ "$error" = "true" ]; then
  ARGS="$ARGS --native-file ./ci/meson/gst-werror.ini"
fi

date -R
meson setup "$builddir" --native-file ./ci/meson/gst-ci-cflags.ini  ${ARGS}
date -R

if command -v ccache
then
  ccache --show-stats
fi

date -R
meson compile -C "$builddir" --jobs "$jobs"
date -R

if command -v ccache
then
  ccache --show-stats
fi
