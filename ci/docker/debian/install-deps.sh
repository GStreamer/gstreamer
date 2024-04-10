#! /bin/bash

set -eux

apt update -y && apt full-upgrade -y
apt install -y $(<./ci/docker/debian/deps.txt)

pip3 install --break-system-packages meson==1.2.3 hotdoc==0.16 python-gitlab tomli junitparser

apt clean all
