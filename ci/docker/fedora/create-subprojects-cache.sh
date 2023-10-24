#! /bin/bash

set -eux

# get gstreamer and make all subprojects available
git clone -b "${GIT_BRANCH}" "${GIT_URL}" /gstreamer
git -C /gstreamer submodule update --init --depth=1
meson subprojects download --sourcedir /gstreamer
./ci/scripts/handle-subprojects-cache.py --build --cache-dir /subprojects /gstreamer/subprojects/

# Run git gc to prune unwanted refs and reduce the size of the image
for i in $(find /subprojects/ -mindepth 1 -maxdepth 1 -type d);
do
    git -C $i gc --aggressive || true;
done

# Now remove the gstreamer clone
rm -rf /gstreamer
