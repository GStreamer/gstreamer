#! /bin/bash

set -eux

bash ./ci/docker/debian/install-deps.sh

bash ./ci/scripts/install-rust.sh

# Configure git for various usage
git config --global user.email "gstreamer@gstreamer.net"
git config --global user.name "Gstbuild Runner"

bash ./ci/scripts/create-subprojects-cache.sh
