#! /bin/bash

set -eux

# Fedora base image disable installing documentation files. See https://pagure.io/atomic-wg/issue/308
# We need them to cleanly build our doc.
sed -i '/tsflags=nodocs/d' /etc/dnf/dnf.conf
dnf -y swap coreutils-single coreutils-full

dnf install -y git-core dnf-plugins-core python3-pip toolbox-experience

# Add rpm fusion repositories in order to access all of the gst plugins
sudo dnf install -y \
  "https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm" \
  "https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm"

dnf upgrade -y && dnf distro-sync -y

# install rest of the extra deps
dnf install -y \
    aalib-devel \
    aom \
    bat \
    busybox \
    intel-mediasdk-devel \
    libaom \
    libaom-devel \
    libcaca-devel \
    libcurl-devel \
    libdav1d \
    libdav1d-devel \
    libdrm-devel \
    ccache \
    cmake \
    clang-devel \
    curl \
    elfutils \
    elfutils-libs \
    elfutils-devel \
    gcc \
    gcc-c++ \
    gdb \
    git-lfs \
    glslc \
    gtk-doc \
    gtk3 \
    gtk3-devel \
    gtk4 \
    gtk4-devel \
    gtest \
    gtest-devel \
    graphene \
    graphene-devel \
    gsl \
    gsl-devel \
    gupnp \
    gupnp-devel \
    gupnp-igd \
    gupnp-igd-devel \
    gssdp \
    gssdp-devel \
    iproute \
    faac-devel \
    ffmpeg \
    ffmpeg-libs \
    ffmpeg-devel \
    flex \
    flite \
    flite-devel \
    libsoup \
    libsoup-devel \
    mono-devel \
    procps-ng \
    patch \
    qconf \
    qt5-linguist \
    qt5-qtbase-devel \
    qt5-qtbase-private-devel \
    qt5-qtdeclarative-devel \
    qt5-qtquickcontrols2-devel \
    qt5-qttools-common \
    qt5-qtwayland-devel \
    qt5-qtx11extras-devel \
    redhat-rpm-config \
    json-glib \
    json-glib-devel \
    libnice \
    libnice-devel \
    libsodium-devel \
    libunwind \
    libunwind-devel \
    libva-devel \
    libyaml-devel \
    libxml2-devel \
    libxslt-devel \
    llvm-devel \
    log4c-devel \
    libxcb-devel \
    libxkbcommon-devel \
    libxkbcommon-x11-devel \
    make \
    nasm \
    neon \
    neon-devel \
    ninja-build \
    nunit \
    npm \
    opencv \
    opencv-devel \
    openjpeg2 \
    openjpeg2-devel \
    qemu-system-x86 \
    SDL2 \
    SDL2-devel \
    sbc \
    sbc-devel \
    x264 \
    x264-libs \
    x264-devel \
    python3 \
    python3-devel \
    python3-libs \
    python3-wheel \
    python3-gobject \
    python3-cairo \
    python3-cairo-devel \
    valgrind \
    vulkan \
    vulkan-devel \
    vulkan-loader \
    mesa-libGL \
    mesa-libGL-devel \
    mesa-libGLU \
    mesa-libGLU-devel \
    mesa-libGLES \
    mesa-libGLES-devel \
    mesa-libOpenCL \
    mesa-libOpenCL-devel \
    mesa-libgbm \
    mesa-libgbm-devel \
    mesa-libd3d \
    mesa-libd3d-devel \
    mesa-libOSMesa \
    mesa-libOSMesa-devel \
    mesa-dri-drivers \
    mesa-vulkan-drivers \
    xset \
    xorg-x11-server-utils \
    xorg-x11-server-Xvfb

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
