#!/bin/bash

set -eux

builddir="$1"

meson_args="${MESON_ARGS:-}"

if [[ -z "$builddir" ]]; then
  echo "Usage: build.sh <build_directory>"
  exit 1
fi

echo ${ABI_CHECK_CACHE}
echo ${ABI_CHECK_DIR}

# nproc works on linux
# sysctl for macos
_jobs=$(nproc || sysctl -n hw.ncpu)
jobs="${FDO_CI_CONCURRENT:-$_jobs}"

install_prefix=/tmp/install
MESON_ARGS="${meson_args} --prefix ${install_prefix} --optimization=g" ./ci/scripts/build.sh build/
ninja -C $builddir install

find ${install_prefix}/lib64 -type f -iname *libgst*.so.* -print0 | xargs -0 -I '{}' bash ${CI_PROJECT_DIR}/ci/scripts/save-abi.sh {} ${ABI_CHECK_DIR} ${install_prefix}/include/gstreamer-1.0

fail_file=abi-compare-failure
if ! find ${ABI_CHECK_CACHE} -type f -print0 | xargs -0 -I '{}' bash ${CI_PROJECT_DIR}/ci/scripts/compare-abi.sh {} ${CI_PROJECT_DIR}/${ABI_CHECK_DIR}/ ${fail_file}
then
  echo ABI comparison failed for the following modules!
  cat ${fail_file}
  rm -rf $install_prefix
  exit 1;
fi

rm -rf $install_prefix
