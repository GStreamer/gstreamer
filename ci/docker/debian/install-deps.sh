#! /bin/bash

set -eux

sudo apt update -y && sudo apt full-upgrade -y
sudo apt install -y $(<./ci/docker/debian/deps.txt)

# These get pulled by other deps
sudo apt remove -y rustc cargo

sudo bash ./ci/scripts/create-pip-config.sh
sudo pip3 install --break-system-packages meson==1.9.0 hotdoc python-gitlab tomli junitparser

sudo apt clean all
