#! /bin/bash

set -eux

# Install a more up to date wayland-protocols
git clone --branch 1.32 https://gitlab.freedesktop.org/wayland/wayland-protocols.git
cd wayland-protocols
meson setup _build --prefix=/usr -Dtests=false
meson install -C _build
cd ..
rm -rf wayland-protocols
