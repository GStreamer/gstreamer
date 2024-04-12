#! /bin/bash

set -e
set -x
# Install dotnet-format
apt update -yqq
apt install -y gnupg apt-transport-https
curl https://packages.microsoft.com/keys/microsoft.asc | gpg --dearmor > microsoft.asc.gpg
mv microsoft.asc.gpg /etc/apt/trusted.gpg.d/
# FIXME: this is bullseye, but image is actually bookworm (testing at the time)
curl -O https://packages.microsoft.com/config/debian/11/prod.list
mv prod.list /etc/apt/sources.list.d/microsoft-prod.list
chown root:root /etc/apt/trusted.gpg.d/microsoft.asc.gpg
chown root:root /etc/apt/sources.list.d/microsoft-prod.list
apt update -yqq
apt install -y dotnet-sdk-7.0
dotnet tool install --global dotnet-format
ln -s ~/.dotnet/tools/dotnet-format /usr/local/bin/dotnet-format

# Build and install gst-indent-1.0
echo "deb-src http://deb.debian.org/debian/ bookworm main" >> /etc/apt/sources.list
apt update
apt-get install --assume-yes devscripts build-essential dpkg-dev wget meson ninja-build
apt-get build-dep --assume-yes indent

git clone https://gitlab.freedesktop.org/gstreamer/gst-indent.git
cd gst-indent

meson setup --prefix=/usr _build
meson install -C _build

# Try it
wget -O gstbayer2rgb.c "https://gitlab.freedesktop.org/gstreamer/gstreamer/-/raw/main/subprojects/gst-plugins-bad/gst/bayer/gstbayer2rgb.c?inline=false"

for i in 1 2; do
indent \
  --braces-on-if-line \
  --case-brace-indentation0 \
  --case-indentation2 \
  --braces-after-struct-decl-line \
  --line-length80 \
  --no-tabs \
  --cuddle-else \
  --dont-line-up-parentheses \
  --continuation-indentation4 \
  --honour-newlines \
  --tab-size8 \
  --indent-level2 \
  --leave-preprocessor-space \
  gstbayer2rgb.c
done;

# clean up
cd ..
rm -rf gst-indent

apt-get remove --assume-yes devscripts build-essential dpkg-dev wget meson ninja-build
apt-get remove --assume-yes libtext-unidecode-perl  libxml-namespacesupport-perl  libxml-sax-base-perl  libxml-sax-perl  libxml-libxml-perl texinfo
apt-get autoremove --assume-yes
