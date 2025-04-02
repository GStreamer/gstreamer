#! /bin/bash

set -eux

bash ./ci/scripts/create-ci-identifier.sh

bash ./ci/docker/fedora/install-deps.sh

# Configure git for various usage
git config --global user.email "gstreamer@gstreamer.net"
git config --global user.name "Gstbuild Runner"

bash ./ci/scripts/install-rust.sh

bash ./ci/scripts/create-container-user.sh

bash ./ci/scripts/create-subprojects-cache.sh

# leftover caches
rm -rf /root/.cache /root/.npm
