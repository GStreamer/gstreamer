#! /bin/bash

set -eux

# Install gdk-pixbuf manually as fedora 34 doesn't build the docs/.devhelp2
git clone --branch gdk-pixbuf-2-40 https://gitlab.gnome.org/GNOME/gdk-pixbuf.git
cd gdk-pixbuf
meson setup _build --prefix=/usr -Ddocs=true
meson install -C _build
cd ..
rm -rf gdk-pixbuf
