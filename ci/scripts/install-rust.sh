#! /bin/bash

set -eux

# Install Rust
RUSTUP_VERSION=1.28.1
RUST_VERSION=1.88.0
RUST_ARCH="x86_64-unknown-linux-gnu"

RUSTUP_URL=https://static.rust-lang.org/rustup/archive/$RUSTUP_VERSION/$RUST_ARCH/rustup-init
curl -o /tmp/rustup-init $RUSTUP_URL

export RUSTUP_HOME="/usr/local/rustup"
export CARGO_HOME="/usr/local/cargo"
export PATH="/usr/local/cargo/bin:$PATH"

chmod +x /tmp/rustup-init;
sudo --preserve-env=RUSTUP_HOME,CARGO_HOME,PATH /tmp/rustup-init -y --no-modify-path --default-toolchain $RUST_VERSION;
rm /tmp/rustup-init;

# We are root while creating the directory, but we want it to
# be accessible to all users
sudo mkdir -p $RUSTUP_HOME $CARGO_HOME
sudo chmod -R a+w $RUSTUP_HOME $CARGO_HOME

cargo install --locked cargo-c --version 0.10.14+cargo-0.89.0
# We don't need them in the build image and they occupy
# 600mb of html files (athough they compress extremely well)
rustup component remove rust-docs

rustup --version
cargo --version
rustc --version
cargo cinstall --version

# Cleanup the registry after install
# so we don't have to save 200mb of the index in the ci image
rm -rf "$CARGO_HOME/registry"
