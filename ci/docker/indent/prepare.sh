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

# Patch indent for crasher bug on very long comments
# https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=1036851
echo "deb-src http://deb.debian.org/debian/ bookworm main" >> /etc/apt/sources.list
apt update

apt-get install --assume-yes devscripts build-essential dpkg-dev wget

apt-get build-dep --assume-yes indent

apt-get source indent

wget -O indent-2.2.12/debian/patches/9999-long-comment-crashfix.patch 'https://git.savannah.gnu.org/cgit/indent.git/patch/?id=02d7fd4c426e4acfa591a6738dec72f7303c1e7e'

echo "9999-long-comment-crashfix.patch" >> indent-2.2.12/debian/patches/series

cat >indent-2.2.12/debian/changelog.new <<-EOF
indent (2.2.12-4gst1) unstable; urgency=medium

  * Pull in bug-fix for crashes on comments longer than 1023 characters.
    See https://bugs.debian.org/cgi-bin/bugreport.cgi?bug=1036851

 -- Tim-Philipp Müller <tim@centricular.com>  Wed, 14 Jun 2023 12:30:00 +0100

EOF
cat indent-2.2.12/debian/changelog >> indent-2.2.12/debian/changelog.new
mv indent-2.2.12/debian/changelog.new indent-2.2.12/debian/changelog

cd indent-2.2.12 && dpkg-buildpackage -us -uc && dpkg -i ../indent_2.2.12-4gst1_amd64.deb

wget -O gstbayer2rgb.c "https://gitlab.freedesktop.org/gstreamer/gstreamer/-/raw/main/subprojects/gst-plugins-bad/gst/bayer/gstbayer2rgb.c?inline=false"

# Try it
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
rm gstbayer2rgb.c
rm -rf indent*
apt-get remove --assume-yes devscripts build-essential dpkg-dev wget
apt-get remove --assume-yes libtext-unidecode-perl  libxml-namespacesupport-perl  libxml-sax-base-perl  libxml-sax-perl  libxml-libxml-perl texinfo
apt-get autoremove --assume-yes
