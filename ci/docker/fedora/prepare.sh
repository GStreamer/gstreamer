#! /bin/bash

set -eux

dnf update && dnf install -y sudo shadow-utils
bash ./ci/scripts/create-ci-identifier.sh
bash ./ci/scripts/create-container-user.sh

sudo -u containeruser bash ./ci/docker/fedora/install-deps.sh
sudo -u containeruser bash ./ci/scripts/install-rust.sh

# Configure git for various usage
sudo -u containeruser git config --global user.email "gstreamer@gstreamer.net"
sudo -u containeruser git config --global user.name "Gstbuild Runner"
# /tmp/clone is where ci-templates cbuild clones the checkout
sudo -u containeruser git config --global --add safe.directory /tmp/clone

sudo -u containeruser bash ./ci/scripts/create-subprojects-cache.sh

# leftover caches
sudo rm -rf /root/
sudo rm -rf /home/containeruser/.cache /home/containeruser/.npm
