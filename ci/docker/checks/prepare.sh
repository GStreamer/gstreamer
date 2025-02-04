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

# Install build dependencies
echo "deb-src http://deb.debian.org/debian/ bookworm main" >> /etc/apt/sources.list
apt update

apt-get install --assume-yes devscripts build-essential dpkg-dev wget  meson ninja-build pkg-config libssl-dev

export PIP_BREAK_SYSTEM_PACKAGES=1
# Install pre-commit
python3 -m pip install --upgrade pip
python3 -m pip install pre-commit==3.6.0

# Install gitlint
python3 -m pip install gitlint

# Install Rust
RUSTUP_VERSION=1.27.1
RUST_VERSION=1.81.0
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

cargo install cargo-c --version 0.10.4+cargo-0.82.0 --locked

rustup --version
cargo --version
rustc --version

# Clean up
apt-get remove --assume-yes devscripts build-essential dpkg-dev wget meson ninja-build pkg-config libssl-dev
apt-get remove --assume-yes libtext-unidecode-perl  libxml-namespacesupport-perl  libxml-sax-base-perl  libxml-sax-perl  libxml-libxml-perl texinfo
apt-get autoremove --assume-yes
