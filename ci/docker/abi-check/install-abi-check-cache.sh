#! /bin/bash

set -eux

meson_args="${MESON_ARGS:-}"

install_prefix=/tmp/install
MESON_ARGS="${meson_args} --prefix ${install_prefix} --optimization=g" ./ci/scripts/build.sh build/
ninja -C build/ install

find ${install_prefix}/lib64 -type f -iname *libgst*.so.* -print0 | xargs -0 -I '{}' bash ./ci/scripts/save-abi.sh {} ${ABI_CHECK_CACHE} ${install_prefix}/include/gstreamer-1.0

rm -rf $install_prefix
