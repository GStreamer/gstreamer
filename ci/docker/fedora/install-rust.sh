#! /bin/bash

set -eux

# Install Rust
RUSTUP_VERSION=1.26.0
RUST_VERSION=1.76.0
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

cargo install --locked cargo-c --version 0.9.30+cargo-0.77.0

rustup --version
cargo --version
rustc --version
