#! /bin/bash

set -eux

# get gstreamer and make all subprojects available
git clone -b "${GIT_BRANCH}" "${GIT_URL}" /gstreamer
git -C /gstreamer submodule update --init --depth=1
meson subprojects download --sourcedir /gstreamer
./ci/scripts/handle-subprojects-cache.py --build --cache-dir /subprojects /gstreamer/subprojects/

# Now remove the gstreamer clone
rm -rf /gstreamer
