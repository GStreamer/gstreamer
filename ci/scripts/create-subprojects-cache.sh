#! /bin/bash

set -eux

# Avoid the cache being owned by root
# and make sure its readable to anyone
sudo mkdir -p /var/cache/subprojects/
sudo chown containeruser:containeruser --recursive /var/cache/subprojects/
sudo chmod --recursive a+r /var/cache/subprojects/

# Path where cbuild checks out the repo
cd /tmp/clone/
sudo chmod --recursive a+r /tmp/clone
sudo chown containeruser:containeruser --recursive /tmp/clone
# get gstreamer and make all subprojects available
git submodule update --init --depth=1
meson subprojects download
./ci/scripts/handle-subprojects-cache.py --build --cache-dir /var/cache/subprojects/ /tmp/clone/subprojects/
