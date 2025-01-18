#! /bin/bash

set -eux

# Fedora base image disable installing documentation files. See https://pagure.io/atomic-wg/issue/308
# We need them to cleanly build our doc.
sed -i '/tsflags=nodocs/d' /etc/dnf/dnf.conf
dnf -y swap coreutils-single coreutils-full
dnf -y swap glibc-minimal-langpack glibc-all-langpacks

# Add rpm fusion repositories in order to access all of the gst plugins
dnf install -y \
  "https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm" \
  "https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm"

# Enable the debuginfo repos so -debug packages are kept in sync
dnf install -y dnf-plugins-core
dnf config-manager --set-enabled '*-debuginfo'

dnf upgrade -y && dnf distro-sync -y

# Install the dependencies of gstreamer
dnf install --setopt=install_weak_deps=false -y $(<./ci/docker/fedora/deps.txt)

# Install devhelp files for hotdoc
dnf install -y glib2-doc gdk-pixbuf2-devel gtk3-devel-docs gtk4-devel-docs libsoup-doc

# Make sure we don't end up installing these from some transient dependency
dnf remove -y "gstreamer1*-devel" rust cargo meson 'fdk-aac-free*'

pip3 install meson==1.5.2 python-gitlab tomli junitparser bs4
pip3 install git+https://github.com/hotdoc/hotdoc.git@8c1cc997f5bc16e068710a8a8121f79ac25cbcce

# Install most debug symbols, except the big ones from things we use
debug_packages=$(rpm -qa | grep -v -i \
    -e bash \
    -e bat \
    -e bluez \
    -e boost \
    -e ccache \
    -e ceph \
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
    -e sequoia \
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

rm -rf /var/cache/dnf /var/log/dnf*
