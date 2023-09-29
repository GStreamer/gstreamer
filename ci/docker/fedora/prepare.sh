#! /bin/bash

set -eux

# Fedora base image disable installing documentation files. See https://pagure.io/atomic-wg/issue/308
# We need them to cleanly build our doc.
sed -i '/tsflags=nodocs/d' /etc/dnf/dnf.conf
dnf -y swap coreutils-single coreutils-full

dnf install -y git-core dnf-plugins-core python3-pip toolbox-experience

# Configure git for various usage
git config --global user.email "gstreamer@gstreamer.net"
git config --global user.name "Gstbuild Runner"

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

# Install common debug symbols
dnf debuginfo-install -y gtk3 \
    glib2 \
    glibc \
    gupnp \
    gupnp-igd \
    gssdp \
    freetype \
    openjpeg \
    gobject-introspection \
    python3 \
    python3-libs \
    python3-gobject \
    libappstream-glib-devel \
    libjpeg-turbo \
    glib-networking \
    libcurl \
    libdrm \
    libsoup \
    libxcb \
    libxkbcommon \
    libxkbcommon-x11 \
    nss \
    nss-softokn \
    nss-softokn-freebl \
    nss-sysinit \
    nss-util \
    openssl \
    openssl-libs \
    openssl-pkcs11 \
    brotli \
    bzip2-libs \
    gpm-libs \
    harfbuzz \
    harfbuzz-icu \
    json-c \
    json-glib \
    libbabeltrace \
    libffi \
    libsrtp \
    libunwind \
    libdvdread \
    mpg123-libs \
    neon \
    orc-compiler \
    orc \
    pixman \
    pulseaudio-libs \
    pulseaudio-libs-glib2 \
    wavpack \
    "libwayland-*" \
    "wayland-*" \
    webrtc-audio-processing \
    ffmpeg \
    ffmpeg-libs \
    faad2-libs \
    libavdevice \
    libmpeg2 \
    faac \
    fdk-aac \
    vulkan-loader \
    x264 \
    x264-libs \
    x265 \
    x265-libs \
    xz \
    xz-libs \
    zip \
    zlib

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
pip3 install meson==1.2.3 hotdoc==0.15 python-gitlab tomli junitparser

# Remove gst-devel packages installed by builddep above
dnf remove -y "gstreamer1*devel"

dnf install -y glib2-doc gdk-pixbuf2-devel gtk3-devel-docs gtk4-devel-docs libsoup-doc

# Install gdk-pixbuf manually as fedora 34 doesn't build the docs/.devhelp2
git clone --branch gdk-pixbuf-2-40 https://gitlab.gnome.org/GNOME/gdk-pixbuf.git
cd gdk-pixbuf
meson setup _build --prefix=/usr -Ddocs=true
meson install -C _build
cd ..
rm -rf gdk-pixbuf

# Install a more up to date wayland-protocols
git clone --branch 1.32 https://gitlab.freedesktop.org/wayland/wayland-protocols.git
cd wayland-protocols
meson setup _build --prefix=/usr -Dtests=false
meson install -C _build
cd ..
rm -rf wayland-protocols

# Install Rust
RUSTUP_VERSION=1.26.0
RUST_VERSION=1.73.0
RUST_ARCH="x86_64-unknown-linux-gnu"

RUSTUP_URL=https://static.rust-lang.org/rustup/archive/$RUSTUP_VERSION/$RUST_ARCH/rustup-init
curl -o rustup-init $RUSTUP_URL

export RUSTUP_HOME="/usr/local/rustup"
export CARGO_HOME="/usr/local/cargo"
export PATH="/usr/local/cargo/bin:$PATH"

chmod +x rustup-init;
./rustup-init -y --no-modify-path --default-toolchain $RUST_VERSION;
rm rustup-init;
chmod -R a+w $RUSTUP_HOME $CARGO_HOME

# Apparently rustup did not do that, and it fails now
cargo install cargo-c --version 0.9.27+cargo-0.74.0

rustup --version
cargo --version
rustc --version

# Install virtme-ng
git clone https://github.com/arighi/virtme-ng.git
pushd virtme-ng
git fetch --tags
git checkout v1.8
./setup.py install --prefix=/usr
popd

# Install fluster
pushd /opt/
git clone https://github.com/fluendo/fluster.git
pushd fluster
git checkout 303a6edfda1701c8bc351909fb1173a0958810c2
./fluster.py download
popd
popd

# get gstreamer and make all subprojects available
git clone -b ${GIT_BRANCH} ${GIT_URL} /gstreamer
git -C /gstreamer submodule update --init --depth=1
meson subprojects download --sourcedir /gstreamer
/gstreamer/ci/scripts/handle-subprojects-cache.py --build --cache-dir /subprojects /gstreamer/subprojects/

# Build a linux image for virtme fluster tests
/gstreamer/ci/scripts/build-linux.sh \
    "https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git" \
    "v6.5.8" \
    /opt/linux/bzImage \
    'MEDIA_SUPPORT' \
    'MEDIA_TEST_SUPPORT' \
    'V4L_TEST_DRIVERS' \
    'CONFIG_VIDEO_VISL'

# Run git gc to prune unwanted refs and reduce the size of the image
for i in $(find /subprojects/ -mindepth 1 -maxdepth 1 -type d);
do
    git -C $i gc --aggressive || true;
done

# Now remove the gstreamer clone
rm -rf /gstreamer

echo "Removing DNF cache"
dnf clean all

rm -R /root/*
rm -rf /var/cache/dnf /var/log/dnf*
