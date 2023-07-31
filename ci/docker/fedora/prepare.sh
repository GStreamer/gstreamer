set -eux

# Fedora base image disable installing documentation files. See https://pagure.io/atomic-wg/issue/308
# We need them to cleanly build our doc.
sed -i "s/tsflags=nodocs//g" /etc/dnf/dnf.conf

dnf install -y git-core dnf-plugins-core python3-pip

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
    intel-mediasdk-devel \
    libaom \
    libaom-devel \
    libcaca-devel \
    libdav1d \
    libdav1d-devel \
    ccache \
    cmake \
    clang-devel \
    elfutils \
    elfutils-libs \
    elfutils-devel \
    gcc \
    gcc-c++ \
    gdb \
    git-lfs \
    glslc \
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
    faac-devel \
    ffmpeg \
    ffmpeg-libs \
    ffmpeg-devel \
    flex \
    flite \
    flite-devel \
    mono-devel \
    procps-ng \
    patch \
    qt5-devel \
    redhat-rpm-config \
    json-glib \
    json-glib-devel \
    libnice \
    libnice-devel \
    libsodium-devel \
    libunwind \
    libunwind-devel \
    libyaml-devel \
    libxml2-devel \
    libxslt-devel \
    llvm-devel \
    log4c-devel \
    make \
    nasm \
    neon \
    neon-devel \
    nunit \
    npm \
    opencv \
    opencv-devel \
    openjpeg2 \
    openjpeg2-devel \
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
    python3-gobject \
    python3-cairo \
    python3-cairo-devel \
    valgrind \
    vulkan \
    vulkan-devel \
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
    mesa-vulkan-drivers \
    wpewebkit \
    wpewebkit-devel \
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
    libsoup \
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
    mpg123-libs \
    neon \
    orc-compiler \
    orc \
    pixman \
    pulseaudio-libs \
    pulseaudio-libs-glib2 \
    wavpack \
    webrtc-audio-processing \
    ffmpeg \
    ffmpeg-libs \
    faad2-libs \
    libavdevice \
    libmpeg2 \
    faac \
    fdk-aac \
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
    gstreamer1-plugins-ugly \
    gstreamer1-plugins-ugly-free \
    gstreamer1-plugins-bad-free \
    gstreamer1-plugins-bad-free-extras \
    gstreamer1-plugins-bad-freeworld \
    gstreamer1-libav \
    gstreamer1-rtsp-server  \
    gstreamer1-vaapi \
    python3-gstreamer1

dnf remove -y meson
# FIXME: Install ninja from rpm when we update our base image as we fail building
# documentation with rust plugins as we the version from F31 we hit:
# `ninja: error: build.ninja:26557: multiple outputs aren't (yet?) supported by depslog; bring this up on the mailing list if it affects you
pip3 install meson==1.1.1 hotdoc==0.15 python-gitlab ninja tomli

# Remove gst-devel packages installed by builddep above
dnf remove -y "gstreamer1*devel"

# FIXME: Why does installing directly with dnf doesn't actually install
# the documentation files?
dnf download glib2-doc gdk-pixbuf2-devel*x86_64* gtk3-devel-docs gtk4-devel-docs
rpm -i --reinstall *.rpm
rm -f *.rpm

# Install Rust
RUSTUP_VERSION=1.26.0
RUST_VERSION=1.71.0
RUST_ARCH="x86_64-unknown-linux-gnu"

dnf install -y wget
RUSTUP_URL=https://static.rust-lang.org/rustup/archive/$RUSTUP_VERSION/$RUST_ARCH/rustup-init
wget $RUSTUP_URL
dnf remove -y wget

export RUSTUP_HOME="/usr/local/rustup"
export CARGO_HOME="/usr/local/cargo"
export PATH="/usr/local/cargo/bin:$PATH"

chmod +x rustup-init;
./rustup-init -y --no-modify-path --profile minimal --default-toolchain $RUST_VERSION;
rm rustup-init;
chmod -R a+w $RUSTUP_HOME $CARGO_HOME

# Apparently rustup did not do that, and it fails now
cargo install cargo-c --version 0.9.21+cargo-0.71

rustup --version
cargo --version
rustc --version

# get gstreamer and make all subprojects available
git clone -b ${GIT_BRANCH} ${GIT_URL} /gstreamer
git -C /gstreamer submodule update --init --depth=1
meson subprojects download --sourcedir /gstreamer
/gstreamer/ci/scripts/handle-subprojects-cache.py --build /gstreamer/subprojects/

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
