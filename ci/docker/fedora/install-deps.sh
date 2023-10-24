#! /bin/bash

set -eux

# Fedora base image disable installing documentation files. See https://pagure.io/atomic-wg/issue/308
# We need them to cleanly build our doc.
sed -i '/tsflags=nodocs/d' /etc/dnf/dnf.conf
dnf -y swap coreutils-single coreutils-full

# Add rpm fusion repositories in order to access all of the gst plugins
sudo dnf install -y \
  "https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm" \
  "https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm"

dnf upgrade -y && dnf distro-sync -y
dnf install -y $(<./ci/docker/fedora/deps.txt)

# Install the dependencies of gstreamer
dnf builddep -y gstreamer1 \
    gstreamer1-plugins-base \
    gstreamer1-plugins-good \
    gstreamer1-plugins-good-extras \
    gstreamer1-plugins-good-qt \
    gstreamer1-plugins-ugly \
    gstreamer1-plugins-ugly-free \
    gstreamer1-plugins-bad-free \
    gstreamer1-plugins-bad-free-extras \
    gstreamer1-plugins-bad-freeworld \
    gstreamer1-libav \
    gstreamer1-rtsp-server  \
    gstreamer1-vaapi \
    python3-gstreamer1

dnf remove -y meson -x ninja-build
pip3 install meson==1.2.3 hotdoc==0.16 python-gitlab tomli junitparser

# Remove gst-devel packages installed by builddep above
dnf remove -y "gstreamer1*devel"

dnf install -y glib2-doc gdk-pixbuf2-devel gtk3-devel-docs gtk4-devel-docs libsoup-doc

# Install most debug symbols, except the big ones from things we use
debug_packages=$(rpm -qa | grep -v -i \
    -e bash \
    -e bat \
    -e bluez \
    -e boost \
    -e ccache \
    -e clang \
    -e cmake \
    -e colord \
    -e compiler-rt \
    -e cpp \
    -e cups \
    -e demos \
    -e flexiblas \
    -e flite \
    -e gcc \
    -e gcc-debuginfo \
    -e gcc-debugsource \
    -e gdal \
    -e gdb \
    -e geocode \
    -e git \
    -e glusterfs \
    -e gpg \
    -e GraphicsMagick \
    -e groff \
    -e gstreamer1 \
    -e java \
    -e leptonica \
    -e libdap \
    -e libdb \
    -e libdnf \
    -e libspatialite \
    -e llvm \
    -e lua \
    -e MUMPS \
    -e NetworkManager \
    -e nodejs \
    -e openblas \
    -e opencv \
    -e openexr \
    -e perl \
    -e poppler \
    -e qemu \
    -e qt5 \
    -e qt6 \
    -e spice \
    -e sqlite \
    -e suitesparse \
    -e systemd \
    -e tesseract \
    -e tests \
    -e tools \
    -e tpm2 \
    -e unbound \
    -e valgrind \
    -e vim \
    -e vtk \
    -e xen \
    -e xerces \
    -e xorg \
)
dnf debuginfo-install -y --best --allowerasing --skip-broken $debug_packages

echo "Removing DNF cache"
dnf clean all

rm -R /root/*
rm -rf /var/cache/dnf /var/log/dnf*
