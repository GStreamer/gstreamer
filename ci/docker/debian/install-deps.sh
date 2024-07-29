#! /bin/bash

set -eux

apt update -y && apt full-upgrade -y
apt install -y $(<./ci/docker/debian/deps.txt)

apt remove -y rustc cargo

pip3 install --break-system-packages meson hotdoc python-gitlab tomli junitparser

apt clean all
