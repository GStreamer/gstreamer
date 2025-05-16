#! /bin/bash

set -eux

# Path where cbuild checks out the repo
cd /tmp/clone/
# get gstreamer and make all subprojects available
git submodule update --init --depth=1
meson subprojects download
./ci/scripts/handle-subprojects-cache.py --build --cache-dir /subprojects /tmp/clone/subprojects/

# Avoid the cache being owned by root
# and make sure its readable to anyone
chown containeruser:containeruser --recursive /subprojects/
chmod --recursive a+r /subprojects/

# Now remove the gstreamer clone
rm -rf /gstreamer
