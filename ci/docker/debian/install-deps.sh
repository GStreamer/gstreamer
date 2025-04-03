#! /bin/bash

set -eux

apt update -y && apt full-upgrade -y
apt install -y $(<./ci/docker/debian/deps.txt)

apt remove -y rustc cargo

bash ./ci/scripts/create-pip-config.sh
pip3 install --break-system-packages meson==1.7.2 hotdoc python-gitlab tomli junitparser

apt clean all
