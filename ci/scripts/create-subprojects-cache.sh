#! /bin/bash

set -eux

branch="${GST_UPSTREAM_BRANCH:-main}"
repo_url="https://gitlab.freedesktop.org/gstreamer/gstreamer.git"

# get gstreamer and make all subprojects available
git clone -b "${branch}" --depth=1 "${repo_url}" /gstreamer
git -C /gstreamer submodule update --init --depth=1
meson subprojects download --sourcedir /gstreamer
./ci/scripts/handle-subprojects-cache.py --build --cache-dir /subprojects /gstreamer/subprojects/

# Avoid the cache being owned by root
# and make sure its readable to anyone
chown containeruser:containeruser --recursive /subprojects/
chmod --recursive a+r /subprojects/

# Now remove the gstreamer clone
rm -rf /gstreamer
