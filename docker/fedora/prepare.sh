set -eu

dnf install -y git-core ninja-build dnf-plugins-core

# Configure git for various usage
git config --global user.email "gst-build@gstreamer.net"
git config --global user.name "Gstbuild Runner"

# Add rpm fusion repositories in order to access all of the gst plugins
dnf install -y "http://download1.rpmfusion.org/free/fedora/rpmfusion-free-release-29.noarch.rpm" \
  "http://download1.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-29.noarch.rpm"

rpm --import "/etc/pki/rpm-gpg/RPM-GPG-KEY-rpmfusion-nonfree-fedora-29"
rpm --import "/etc/pki/rpm-gpg/RPM-GPG-KEY-rpmfusion-free-fedora-29"
dnf upgrade -y

# Enable the cisco openh264 repo
dnf config-manager --set-enabled fedora-cisco-openh264

# install rest of the extra deps
dnf install -y \
    aom \
    aom-extra-tools \
    libaom \
    libaom-devel \
    ccache \
    cmake \
    elfutils \
    elfutils-libs \
    elfutils-devel \
    gcc \
    gcc-c++ \
    gdb \
    git-lfs \
    gtk3 \
    gtk3-devel \
    graphene \
    graphene-devel \
    gsl \
    gsl-devel \
    ffmpeg \
    ffmpeg-libs \
    ffmpeg-devel \
    flite \
    flite-devel \
    mono-devel \
    procps-ng \
    patch \
    redhat-rpm-config \
    json-glib \
    json-glib-devel \
    libnice \
    libnice-devel \
    libunwind \
    libunwind-devel \
    neon \
    neon-devel \
    nunit \
    opencv \
    opencv-devel \
    openjpeg2 \
    openjpeg2-devel \
    openh264 \
    openh264-devel \
    SDL2 \
    SDL2-devel \
    sbc \
    sbc-devel \
    x264 \
    x264-libs \
    x264-devel \
    python3-gobject \
    python3-cairo \
    python3-cairo-devel \
    valgrind \
    vulkan \
    vulkan-devel \
    mesa-omx-drivers \
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
    xorg-x11-server-utils \
    xorg-x11-server-Xvfb

# Install common debug symbols
dnf debuginfo-install -y gtk3 \
    glib2 \
    glibc \
    freetype \
    openjpeg \
    gobject-introspection \
    python3 \
    python3-gobject \
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

pip3 install meson==0.49.2

# Install the dependencies of gstreamer
dnf builddep -y gstreamer1 \
    gstreamer1-plugins-base \
    gstreamer1-plugins-good \
    gstreamer1-plugins-good-extras \
    gstreamer1-plugins-ugly \
    gstreamer1-plugins-ugly-free \
    gstreamer1-plugins-bad-nonfree \
    gstreamer1-plugins-bad-free \
    gstreamer1-plugins-bad-free-extras \
    gstreamer1-plugins-bad-freeworld \
    gstreamer1-libav \
    gstreamer1-rtsp-server  \
    gstreamer1-vaapi \
    python3-gstreamer1 \
    -x meson

# Remove gst-devel packages installed by builddep above
dnf remove -y "gstreamer1*devel"

# Remove Qt5 devel packages as we haven't tested building it and
# it leads to build issues in examples.
dnf remove -y "qt5-qtbase-devel"

# get gst-build and make all subprojects available
git clone git://anongit.freedesktop.org/gstreamer/gst-build /gst-build/
cd /gst-build
meson subprojects download

