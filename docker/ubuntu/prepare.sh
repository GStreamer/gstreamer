set -eu

# make source packages available in order to figure out build dependencies
sed -i "s/# deb-src/deb-src/g" /etc/apt/sources.list

apt update
apt build-dep -y \
    orc \
    gstreamer1.0 \
    gst-plugins-base1.0 \
    gst-plugins-good1.0 \
    gst-plugins-bad1.0 \
    gst-plugins-ugly1.0 \
    gst-libav1.0 \
    gst-rtsp-server1.0 \
    gst-python1.0 \
    gstreamer-vaapi \
    gstreamer-editing-services1.0

apt install -y \
    ccache \
    gdb \
    git \
    xvfb \
    python3-pip \
    wget \
    unzip

pip3 install meson

# Configure git for various usage
git config --global user.email "gst-build@gstreamer.net"
git config --global user.name "Gstbuild Runner"

# Setup Android toolchain
/root/android-download-ndk.sh
/root/android-create-toolchain.sh arm64 28
rm -rf /opt/android-ndk

# get gst-build and make all subprojects available
git clone git://anongit.freedesktop.org/gstreamer/gst-build '/gst-build/'
cd '/gst-build/' && meson build/ && rm -rf build/