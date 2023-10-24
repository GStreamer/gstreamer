#! /bin/bash

set -eux

# Configure git for various usage
git config --global user.email "gstreamer@gstreamer.net"
git config --global user.name "Gstbuild Runner"

bash ./ci/docker/fedora/install-deps.sh

bash ./ci/docker/fedora/install-gdk-pixbuf.sh

bash ./ci/docker/fedora/install-wayland-protocols.sh

bash ./ci/docker/fedora/install-rust.sh

bash ./ci/docker/fedora/virtme-fluster-setup.sh

bash ./ci/docker/fedora/create-subprojects-cache.sh
